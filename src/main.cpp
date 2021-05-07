#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cocos2d.h>
#include <imgui.h>
#include <imgui_hook.h>
#include <MinHook.h>
#include <queue>
#include <mutex>
#include <fstream>

using namespace cocos2d;

const char* getNodeName(CCNode* node) {
    const char* name = typeid(*node).name() + 6;
    return name;
}

void clipboardText(const char* text) {
    if (!OpenClipboard(NULL)) return;
    if (!EmptyClipboard()) return;
    auto len = std::strlen(text);
    auto mem = GlobalAlloc(GMEM_MOVEABLE, len + 1);
    memcpy(GlobalLock(mem), text, len + 1);
    GlobalUnlock(mem);
    SetClipboardData(CF_TEXT, mem);
    CloseClipboard();
}

std::queue<std::function<void()>> threadFunctions;
std::mutex threadFunctionsMutex;

void generateTree(CCNode* node, unsigned int i = 0) {
    if (ImGui::TreeNode(node, node->getTag() == -1 ? "[%d] %s" : "[%d] %s (%d)", i, getNodeName(node), node->getTag())) {
        if (ImGui::TreeNode(node + 1, "Attributes")) {
            if (ImGui::Button("Delete")) {
                node->removeFromParentAndCleanup(true);
                ImGui::TreePop();
                ImGui::TreePop();
                return;
            }
            ImGui::SameLine();
            if (ImGui::Button("Add Child")) {
                ImGui::OpenPopup("Add Child");
            }

            if (ImGui::BeginPopupModal("Add Child")) {
                static int item = 0;
                ImGui::Combo("Node", &item, "CCNode\0CCLabelBMFont\0CCLabelTTF\0CCSprite\0CCMenuItemSpriteExtra\0");

                static int tag = -1;
                ImGui::InputInt("Tag", &tag);

                static char text[256];
                static char labelFont[256] = "bigFont.fnt";
                if (item == 1) {
                    ImGui::InputText("Text", text, 256);
                    ImGui::InputText("Font", labelFont, 256);
                }
                static int fontSize = 20;
                if (item == 2) {
                    ImGui::InputText("Text", text, 256);
                    ImGui::InputInt("Font Size", &fontSize);
                }
                static bool frame = false;
                if (item == 3 || item == 4) {
                    ImGui::InputText("Texture", text, 256);
                    ImGui::Checkbox("Frame", &frame);
                }

                ImGui::Separator();

                if (ImGui::Button("Add")) {
                    threadFunctionsMutex.lock();
                    threadFunctions.push([node] {
                        CCNode* _child = nullptr;
                        switch (item) {
                        case 0:
                            _child = CCNode::create();
                            break;
                        case 1: {
                            auto child = CCLabelBMFont::create(text, labelFont);
                            _child = child;
                            break;
                        }
                        case 2: {
                            auto child = CCLabelTTF::create(text, "Arial", fontSize);
                            _child = child;
                            break;
                        }
                        case 3: {
                            CCSprite* child;
                            if (frame)
                                child = CCSprite::createWithSpriteFrameName(text);
                            else
                                child = CCSprite::create(text);
                            _child = child;
                            break;
                        }
                        case 4: {
                            CCSprite* sprite;
                            if (frame)
                                sprite = CCSprite::createWithSpriteFrameName(text);
                            else
                                sprite = CCSprite::create(text);
                            // _child = CCMenuItemSpriteExtra::create(sprite, sprite, nullptr, nullptr);
                            break;
                        }
                        default:
                            return;
                        }
                        if (_child != nullptr) {
                            _child->setTag(tag);
                            node->addChild(_child);
                        }
                    });
                    threadFunctionsMutex.unlock();

                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::Text("Addr: 0x%p", node);
            ImGui::SameLine();
            if (ImGui::Button("Copy")) {
                std::stringstream stream;
                stream << std::uppercase << std::hex << reinterpret_cast<uintptr_t>(node);
                clipboardText(stream.str().c_str());
            }
            if (node->getUserData()) {
                ImGui::Text("User data: 0x%p", node->getUserData());
            }

            auto pos = node->getPosition();
            float _pos[2] = { pos.x, pos.y };
            ImGui::DragFloat2("Position", _pos);
            node->setPosition({ _pos[0], _pos[1] });

            float _scale[3] = { node->getScale(), node->getScaleX(), node->getScaleY() };
            ImGui::DragFloat3("Scale", _scale, 0.025);
            // amazing
            if (node->getScale() != _scale[0])
                node->setScale(_scale[0]);
            else {
                node->setScaleX(_scale[1]);
                node->setScaleY(_scale[2]);
            }

            float _rot[3] = { node->getRotation(), node->getRotationX(), node->getRotationY() };
            ImGui::DragFloat3("Rotation", _rot);
            if (node->getRotation() != _rot[0])
                node->setRotation(_rot[0]);
            else {
                node->setRotationX(_rot[1]);
                node->setRotationY(_rot[2]);
            }

            float _skew[2] = { node->getSkewX(), node->getSkewY() };
            ImGui::DragFloat2("Skew", _skew);
            node->setSkewX(_skew[0]);
            node->setSkewY(_skew[1]);

            auto anchor = node->getAnchorPoint();
            ImGui::DragFloat2("Anchor Point", &anchor.x, 0.05f, 0.f, 1.f);
            node->setAnchorPoint(anchor);

            int zOrder = node->getZOrder();
            ImGui::InputInt("Z", &zOrder);
            if (node->getZOrder() != zOrder)
                node->setZOrder(zOrder);
            
            auto visible = node->isVisible();
            ImGui::Checkbox("Visible", &visible);
            if (visible != node->isVisible())
                node->setVisible(visible);

            if (dynamic_cast<CCRGBAProtocol*>(node) != nullptr) {
                auto rgbaNode = dynamic_cast<CCRGBAProtocol*>(node);
                auto color = rgbaNode->getColor();
                float _color[4] = { color.r / 255.f, color.g / 255.f, color.b / 255.f, rgbaNode->getOpacity() / 255.f };
                ImGui::ColorEdit4("Color", _color);
                rgbaNode->setColor({
                    static_cast<GLubyte>(_color[0] * 255),
                    static_cast<GLubyte>(_color[1] * 255),
                    static_cast<GLubyte>(_color[2] * 255)
                });
                rgbaNode->setOpacity(_color[3] * 255);
            }
            if (dynamic_cast<CCLabelProtocol*>(node) != nullptr) {
                auto labelNode = dynamic_cast<CCLabelProtocol*>(node);
                auto labelStr = labelNode->getString();
                char text[256];
                strcpy_s(text, labelStr);
                ImGui::InputText("Text", text, 256);
                if (strcmp(text, labelStr)) {
                    threadFunctionsMutex.lock();
                    threadFunctions.push([labelNode, text]() {
                        labelNode->setString(text);
                    });
                    threadFunctionsMutex.unlock();
                }
            }

            ImGui::TreePop();
        }
        
        auto children = node->getChildren();
        for (unsigned int i = 0; i < node->getChildrenCount(); ++i) {
            auto child = children->objectAtIndex(i);
            generateTree(dynamic_cast<CCNode*>(child), i);
        }
        ImGui::TreePop();
    }
}

bool g_showWindow = true;

void RenderMain() {
    if (g_showWindow) {
        auto& style = ImGui::GetStyle();
        style.ColorButtonPosition = ImGuiDir_Left;

        auto director = CCDirector::sharedDirector();
        // thx andre
        const bool enableTouch = !ImGui::GetIO().WantCaptureMouse;
        director->getTouchDispatcher()->setDispatchEvents(enableTouch);
        if (ImGui::Begin("cocos2d explorer"), nullptr, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar) {
            auto curScene = director->getRunningScene();
            generateTree(curScene);
        }
        ImGui::End();
    }
}

inline void(__thiscall* dispatchKeyboardMSG)(void* self, int key, bool down);
void __fastcall dispatchKeyboardMSGHook(void* self, void*, int key, bool down) {
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    else if (down && key == 'K') {
        g_showWindow ^= 1;
        if (!g_showWindow)
            CCDirector::sharedDirector()->getTouchDispatcher()->setDispatchEvents(true);
    }
    dispatchKeyboardMSG(self, key, down);
}

inline void(__thiscall* schUpdate)(CCScheduler* self, float dt);
void __fastcall schUpdateHook(CCScheduler* self, void*, float dt) {
    threadFunctionsMutex.lock();
    while (!threadFunctions.empty()) {
        threadFunctions.back()();
        threadFunctions.pop();
    }
    threadFunctionsMutex.unlock();
    return schUpdate(self, dt);
}

DWORD WINAPI my_thread(void* hModule) {
#if _DEBUG
    AllocConsole();
    std::ofstream conout("CONOUT$", std::ios::out);
    std::ifstream conin("CONIN$", std::ios::in);
    std::cout.rdbuf(conout.rdbuf());
    std::cin.rdbuf(conin.rdbuf());  
#endif
    auto cocosBase = GetModuleHandleA("libcocos2d.dll");
    MH_CreateHook(
        GetProcAddress(cocosBase, "?dispatchKeyboardMSG@CCKeyboardDispatcher@cocos2d@@QAE_NW4enumKeyCodes@2@_N@Z"),
        &dispatchKeyboardMSGHook,
        reinterpret_cast<void**>(&dispatchKeyboardMSG)
    );
    MH_CreateHook(
        GetProcAddress(cocosBase, "?update@CCScheduler@cocos2d@@UAEXM@Z"),
        &schUpdateHook,
        reinterpret_cast<void**>(&schUpdate)
    );
    MH_EnableHook(MH_ALL_HOOKS);

#if 0
    std::getline(std::cin, std::string());

    MH_Uninitialize();
    conout.close();
    conin.close();
    FreeConsole();
    ImGuiHook::Unload();
    FreeLibraryAndExitThread(reinterpret_cast<HMODULE>(hModule), 0);
#endif
    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID _) {
    if (reason == DLL_PROCESS_ATTACH) {
        CreateThread(0, 0x1000, my_thread, module, 0, 0);
        ImGuiHook::Load(RenderMain);
    }
    return TRUE;
}