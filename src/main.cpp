#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cocos2d.h>
#include <imgui.h>
#include <MinHook.h>
#include <sstream>
#include <imgui-hook.hpp>
#include <string_view>
#include <imgui/misc/cpp/imgui_stdlib.h>
#include <filesystem>
#include "utils.hpp"
#include "ModUtils.hpp"
#include "HooksUtils.hpp"

using namespace cocos2d;

/*
CCNode* curScene = pCCNode;
if (curScene) {
	generateTree(curScene);
}
*/
CCNode* pCCNode;
inline CCScene* (__cdecl* CCScene_create)();
CCScene* CCScene_create_H() {
	CCScene* pCCScene = CCScene_create();
	pCCNode = pCCScene;
	return pCCScene;
}

const char* get_node_name(CCNode* node) {
	// works because msvc's typeid().name() returns undecorated name
	// typeid(CCNode).name() == "class cocos2d::CCNode"
	// the + 6 gets rid of the class prefix
	// "class cocos2d::CCNode" + 6 == "cocos2d::CCNode"
	return typeid(*node).name() + 6;
}

static CCNode* selected_node = nullptr;
static bool reached_selected_node = false;
static CCNode* hovered_node = nullptr;

CCPoint OldAnchorPoint;
CCPoint OldPosition;
float OldRotation;
float OldScaleX;
float OldScaleY;
CCSize OldContentSize;
bool OldVisible;

void SaveOldPropertiesStore(CCNode* node) {
	if (!node) return;
	OldAnchorPoint = node->getAnchorPoint();
	OldPosition = (node->getPosition());
	OldRotation = (node->getRotation());
	OldScaleX = (node->getScaleX());
	OldScaleY = (node->getScaleY());
	OldContentSize = (node->getContentSize());
	OldVisible = (node->isVisible());
}

void render_node_tree(CCNode* node, unsigned int index = 0) {
	if (!node) return;
	std::stringstream stream;
	stream << "[" << index << "] " << get_node_name(node);
	if (node->getTag() != -1)
		stream << " (" << node->getTag() << ")";
	const auto children_count = node->getChildrenCount();
	if (children_count)
		stream << " {" << children_count << "}";

	auto flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_SpanFullWidth;
	if (selected_node == node) {
		flags |= ImGuiTreeNodeFlags_Selected;
		reached_selected_node = true;
	}
	if (node->getChildrenCount() == 0)
		flags |= ImGuiTreeNodeFlags_Leaf;

	ImGui::PushStyleColor(ImGuiCol_Text, node->isVisible() ? ImVec4{ 1.f, 1.f, 1.f, 1.f } : ImVec4{ 0.8f, 0.8f, 0.8f, 1.f });
	const bool is_open = ImGui::TreeNodeEx(node, flags, stream.str().c_str());
	if (ImGui::IsItemClicked()) {
		if (node == selected_node) {
			selected_node = nullptr;
			reached_selected_node = false;
		}
		else {
			SaveOldPropertiesStore(node);
			selected_node = node;
			reached_selected_node = true;
		}
	}
	if (ImGui::IsItemHovered())
		hovered_node = node;
	if (is_open) {
		auto children = node->getChildren();
		for (unsigned int i = 0; i < children_count; ++i) {
			auto child = children->objectAtIndex(i);
			render_node_tree(static_cast<CCNode*>(child), i);
		}
		ImGui::TreePop();
	}
	ImGui::PopStyleColor();
}

#define CONCAT_(a, b) a ## b
#define CONCAT(a, b) CONCAT_(a, b)

void render_node_properties(CCNode* node) {
	if (!node) return;
	if (ImGui::Button("Delete")) {
		node->removeFromParentAndCleanup(true);
		return;
	}
	ImGui::SameLine();
	bool R = (GetAsyncKeyState(0x52) & 0x8000);
	if (ImGui::Button("Reset All") or R) {
		node->setAnchorPoint(OldAnchorPoint);
		node->setPosition(OldPosition);
		node->setRotation(OldRotation);
		node->setScaleX(OldScaleX);
		node->setScaleY(OldScaleY);
		node->setContentSize(OldContentSize);
		node->setVisible(OldVisible);
	}
	ImGui::Text("Addr: 0x%p", node);
	ImGui::SameLine();
	if (ImGui::Button("Copy##copyaddr")) {
		std::stringstream stream;
		stream << std::uppercase << std::hex << reinterpret_cast<uintptr_t>(node);
		set_clipboard_text(stream.str());
	}
	if (node->getUserData())
		ImGui::Text("User data: 0x%p", node->getUserData());

#define GET_SET_FLOAT2(name, label) { \
	auto point = node->get##name(); \
	if (ImGui::DragFloat2(label, reinterpret_cast<float*>(&point))) \
		node->set##name(point); \
}

#define GET_SET_INT(name, label) { \
	auto value = node->get##name(); \
	if (ImGui::InputInt(label, &value)) \
		node->set##name(value); \
}

#define GET_SET_CHECKBOX(name, label) { \
	auto value = node->is##name(); \
	if (ImGui::Checkbox(label, &value)) \
		node->set##name(value); \
}

	GET_SET_FLOAT2(Position, "Position");

#define dragXYBothIDK(name, label, speed) { \
	float values[3] = { node->get##name(), node->CONCAT(get, CONCAT(name, X))(), node->CONCAT(get, CONCAT(name, Y))() }; \
	if (ImGui::DragFloat3(label, values, speed)) { \
		if (node->get##name() != values[0]) \
			node->set##name(values[0]); \
		else { \
			node->CONCAT(set, CONCAT(name, X))(values[1]); \
			node->CONCAT(set, CONCAT(name, Y))(values[2]); \
		} \
	} \
}
	dragXYBothIDK(Scale, "Scale", 0.025f);
	dragXYBothIDK(Rotation, "Rotation", 1.0f);

#undef dragXYBothIDK

	{
		auto anchor = node->getAnchorPoint();
		if (ImGui::DragFloat2("Anchor Point", &anchor.x, 0.05f, 0.f, 1.f))
			node->setAnchorPoint(anchor);
	}

	GET_SET_FLOAT2(ContentSize, "Content Size");
	GET_SET_INT(ZOrder, "Z Order");
	GET_SET_CHECKBOX(Visible, "Visible");

	if (auto rgba_node = dynamic_cast<CCNodeRGBA*>(node); rgba_node) {
		auto color = rgba_node->getColor();
		float colorValues[4] = {
			color.r / 255.f,
			color.g / 255.f,
			color.b / 255.f,
			rgba_node->getOpacity() / 255.f
		};
		if (ImGui::ColorEdit4("Color", colorValues)) {
			rgba_node->setColor({
				static_cast<GLubyte>(colorValues[0] * 255),
				static_cast<GLubyte>(colorValues[1] * 255),
				static_cast<GLubyte>(colorValues[2] * 255)
				});
			rgba_node->setOpacity(static_cast<GLubyte>(colorValues[3] * 255.f));
		}
	}

	if (auto label_node = dynamic_cast<CCLabelProtocol*>(node); label_node) {
		std::string str = label_node->getString();
		if (ImGui::InputTextMultiline("Text", &str, { 0, 50 }))
			label_node->setString(str.c_str());
	}

	if (auto sprite_node = dynamic_cast<CCSprite*>(node); sprite_node) {
		auto* texture = sprite_node->getTexture();

		auto* texture_cache = CCTextureCache::sharedTextureCache();
		auto* cached_textures = public_cast(texture_cache, m_pTextures);
		CCDictElement* el;
		CCDICT_FOREACH(cached_textures, el) {
			if (el->getObject() == texture) {
				ImGui::TextWrapped("Texture name: %s", el->getStrKey());
				break;
			}
		}

		auto* frame_cache = CCSpriteFrameCache::sharedSpriteFrameCache();
		auto* cached_frames = public_cast(frame_cache, m_pSpriteFrames);
		const auto rect = sprite_node->getTextureRect();
		CCDICT_FOREACH(cached_frames, el) {
			auto* frame = static_cast<CCSpriteFrame*>(el->getObject());
			if (frame->getTexture() == texture && frame->getRect() == rect) {
				ImGui::Text("Frame name: %s", el->getStrKey());
				break;
			}
		}
	}

	if (auto menu_item_node = dynamic_cast<CCMenuItem*>(node); menu_item_node) {
		const auto selector = public_cast(menu_item_node, m_pfnSelector);
		const auto addr = format_addr(union_cast<void*>(selector));
		ImGui::Text("CCMenuItem selector: %s", addr.c_str());
		ImGui::SameLine();
		if (ImGui::Button("Copy##copyselector")) {
			set_clipboard_text(addr);
		}
	}
}

void render_node_highlight(CCNode* node, bool selected) {
	if (!node) return;
	auto& foreground = *ImGui::GetForegroundDrawList();
	auto parent = node->getParent();
	auto bounding_box = node->boundingBox();
	CCPoint bb_min(bounding_box.getMinX(), bounding_box.getMinY());
	CCPoint bb_max(bounding_box.getMaxX(), bounding_box.getMaxY());

	auto camera_parent = node;
	while (camera_parent) {
		auto camera = camera_parent->getCamera();

		float off_x, off_y, off_z;
		camera->getEyeXYZ(&off_x, &off_y, &off_z);
		const CCPoint offset(off_x, off_y);
		bb_min -= offset;
		bb_max -= offset;

		camera_parent = camera_parent->getParent();
	}

	auto min = cocos_to_vec2(parent ? parent->convertToWorldSpace(bb_min) : bb_min);
	auto max = cocos_to_vec2(parent ? parent->convertToWorldSpace(bb_max) : bb_max);
	foreground.AddRectFilled(min, max, selected ? IM_COL32(200, 200, 255, 60) : IM_COL32(255, 255, 255, 70));
}

double update_node_pos_step;
double update_node_rot_step;
void update_node_by_key(CCNode* node) {
	if (!node) return;
	//delay
	Sleep(3);
	//pos
	bool W = (GetAsyncKeyState(0x57) & 0x8000);
	bool S = (GetAsyncKeyState(0x53) & 0x8000);
	bool D = (GetAsyncKeyState(0x44) & 0x8000);
	bool A = (GetAsyncKeyState(0x41) & 0x8000);
	if (W) node->setPositionY(node->getPositionY() + update_node_pos_step);
	if (S) node->setPositionY(node->getPositionY() - update_node_pos_step);
	if (D) node->setPositionX(node->getPositionX() + update_node_pos_step);
	if (A) node->setPositionX(node->getPositionX() - update_node_pos_step);
	if (!W && !A && !S && !D) update_node_pos_step = 0.001;
	else update_node_pos_step = update_node_pos_step + 0.01;
	//rot
	bool Q = (GetAsyncKeyState(0x51) & 0x8000);
	bool E = (GetAsyncKeyState(0x45) & 0x8000);
	if (Q) node->setRotation(node->getRotation() - update_node_rot_step);
	if (E) node->setRotation(node->getRotation() + update_node_rot_step);
	if (!Q && !E) update_node_rot_step = 0.001;
	else update_node_rot_step = update_node_rot_step + 0.01;
}

bool show_window = false;
static ImFont* g_font = nullptr;

void draw() {
	if (g_font) ImGui::PushFont(g_font);
	if (show_window) {
		static bool highlight = false;
		hovered_node = nullptr;
		if (ImGui::Begin("- GD Scenes Explorer -", nullptr, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_MenuBar)) {
			
			ImGui::BeginMenuBar();
			ImGui::MenuItem("Highlight", nullptr, &highlight);
			ImGui::EndMenuBar();

			const auto avail = ImGui::GetContentRegionAvail();

			ImGui::BeginChild("explorer.tree", ImVec2(avail.x * 0.5f, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

			CCNode* node = pCCNode;
			if (node) {
				reached_selected_node = false;
				render_node_tree(node);
			}

			ImGui::EndChild();

			if (!reached_selected_node)
				selected_node = nullptr;

			ImGui::SameLine();

			ImGui::BeginChild("explorer.options");

			if (selected_node) {
				render_node_properties(selected_node);
			}
			else ImGui::Text("Select a node to edit its properties");

			ImGui::EndChild();
		}

		//inf
		{
			ImGui::Text("\n""GD Scenes Explorer its a port of CocosExplorer by Mat");

			ImGui::BeginListBox("");
			ImGui::Text(
				/*/*/"Keys:"
				"\n" "* F1 - Show this window"
				"\n" "* SHIFT - Highlight"
				"\n" "* W/A/S/D - Move selected node"
				"\n" "* Q/E - Rotate selected node"
				"\n" "* R - Reset selected node"
			);
			ImGui::EndListBox();
		};

		ImGui::End();//- GD Scenes Explorer -

		//highlight
		bool VK_SHIFT_STATE = (GetAsyncKeyState(VK_SHIFT) & 0x8000);
		if ((highlight || VK_SHIFT_STATE) && (selected_node || hovered_node)) {
			if (selected_node)
				render_node_highlight(selected_node, true);
			if (hovered_node)
				render_node_highlight(hovered_node, false);
		}

		//update_node_by_key
		if (selected_node) update_node_by_key(selected_node);

		//inf text
		{
			ImGuiIO& io = ImGui::GetIO();
			ImGui::SetNextWindowPos(ImVec2(10, (io.DisplaySize.y) - 10), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
			ImGui::Begin("", nullptr,
				ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize
			);
			if (ImGui::SmallButton("Release: v1")) {
				CCApplication::sharedApplication()->openURL("https://github.com/user95401/GDScenesExplorer/releases");
			}
			ImGui::SameLine();
			ImGui::Text(std::format(
				"PosStep: {}, RotStep: {}, HaveSelectedNode: {}, HaveHoveredNode: {}",
				round(update_node_pos_step * 1000) / 1000,
				round(update_node_rot_step * 1000) / 1000,
				(bool)selected_node,
				(bool)hovered_node
			).c_str());
			ImGui::End();
		}
	}

	if (ImGui::GetTime() < 5.0) {
		ImGui::SetNextWindowPos({ 10, 10 });
		ImGui::Begin("", nullptr,
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings);
		ImGui::Text("GD Scenes Explorer loaded, press F1 to toggle");
		ImGui::End();
	}
	if (g_font) ImGui::PopFont();
}

#include <fstream>
void init() {
	if (!std::filesystem::exists("GDScenesExplorer.ini")) {
		std::ofstream("GDScenesExplorer.ini") <<
			"[Window][- GD Scenes Explorer -]\n"
			"Pos=10,10\n"
			"Size=926,768\n"
			;
	}
	ImGui::GetIO().IniFilename = "GDScenesExplorer.ini";

	auto& style = ImGui::GetStyle();
	style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
	style.WindowBorderSize = 0;
	style.ColorButtonPosition = ImGuiDir_Left;

	g_font = ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\comic.ttf", 22.f);
	g_font->Scale = 1.0f;

	auto colors = style.Colors;
	colors[ImGuiCol_FrameBg] = ImVec4(0.31f, 0.31f, 0.31f, 0.54f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.59f, 0.59f, 0.59f, 0.40f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.61f, 0.61f, 0.61f, 0.67f);
	colors[ImGuiCol_TitleBg] = colors[ImGuiCol_TitleBgActive] = ImVec4(0.2f, 0.2f, 0.2f, 1.00f);
	colors[ImGuiCol_CheckMark] = ImVec4(0.89f, 0.89f, 0.89f, 1.00f);
	colors[ImGuiCol_TextSelectedBg] = ImVec4(0.71f, 0.71f, 0.71f, 0.35f);
}

DWORD WINAPI my_thread(void* hModule) {
	//ImGuiHookstp
	ImGuiHook::setInitFunction(init);
	ImGuiHook::setToggleKey(VK_F1);
	ImGuiHook::setRenderFunction(draw);
	ImGuiHook::setToggleCallback([]() {
		show_window = !show_window;
	});
	ImGuiHook::setInitFunction(init);
	MH_Initialize();
	ImGuiHook::setupHooks([](void* target, void* hook, void** trampoline) {
		HooksUtils::CreateHook(target, hook, trampoline);
	});
	CC_HOOK("?create@CCScene@cocos2d@@SAPAV12@XZ", CCScene_create);
	return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
	if (reason != DLL_PROCESS_ATTACH) return TRUE;
	CreateThread(0, 0, my_thread, module, 0, 0);
	return TRUE;
}
