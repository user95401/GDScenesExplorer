#include "pch.h"
#include <imgui.h>
#include "opengl-imgui-hook/imgui_hook.h"
#include <minhook/include/MinHook.h>


#define _NODE_NAME(type) if (dynamic_cast<type*>(node)) return #type"";
const char* getNodeName(CCNode* node) {
    _NODE_NAME(CCMenu);
    _NODE_NAME(CCLayer);
    _NODE_NAME(CCSprite);
    _NODE_NAME(CCScene);
    return "CCNode";
}

void generateTree(CCNode* node, int i = 0) {
    //                                                ew
    if (ImGui::TreeNode(node, node->getTag() == -1 ? "[%d] %s" : "[%d] %s (%d)", i, getNodeName(node), node->getTag())) {
        if (ImGui::TreeNode(node + 1, "Attributes")) {
            if (ImGui::Button("Delete")) {
                node->removeFromParentAndCleanup(true);
                ImGui::TreePop();
                ImGui::TreePop();
                return;
            }

            auto pos = node->getPosition();
            float _pos[2] = { pos.x, pos.y };
            ImGui::InputFloat2("Position", _pos);
            node->setPosition({ _pos[0], _pos[1] });

            float _scale[3] = { node->getScale(), node->getScaleX(), node->getScaleY() };
            ImGui::InputFloat3("Scale", _scale);
            // amazing
            if (node->getScale() != _scale[0])
                node->setScale(_scale[0]);
            else {
                node->setScaleX(_scale[1]);
                node->setScaleY(_scale[2]);
            }

            float _rot[3] = { node->getRotation(), node->getRotationX(), node->getRotationY() };
            ImGui::InputFloat3("Rotation", _rot);
            if (node->getRotation() != _rot[0])
                node->setRotation(_rot[0]);
            else {
                node->setRotationX(_rot[1]);
                node->setRotationY(_rot[2]);
            }

            float _skew[2] = { node->getSkewX(), node->getSkewY() };
            ImGui::InputFloat2("Skew", _skew);
            node->setSkewX(_skew[0]);
            node->setSkewY(_skew[1]);

            int zOrder = node->getZOrder();
            ImGui::InputInt("Z", &zOrder);
            if (node->getZOrder() != zOrder)
                node->setZOrder(zOrder);

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

            ImGui::TreePop();
        }
        
        auto children = node->getChildren();
        for (int i = 0; i < node->getChildrenCount(); ++i) {
            auto child = children->objectAtIndex(i);
            generateTree(dynamic_cast<CCNode*>(child), i);
        }
        ImGui::TreePop();
    }
}

void RenderMain() {
    ImGui::Begin("cocos2d explorer");
    auto director = cocos2d::CCDirector::sharedDirector();
    // thank u andre
    const bool enableTouch = !ImGui::GetIO().WantCaptureMouse;
    director->getTouchDispatcher()->setDispatchEvents(enableTouch);
    auto curScene = director->getRunningScene();
    generateTree(curScene);
    ImGui::End();
}

inline void(__thiscall* dispatchKeyboardMSG)(void* self, int key, bool down);
void __fastcall dispatchKeyboardMSGHook(void* self, void*, int key, bool down) {
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    dispatchKeyboardMSG(self, key, down);
}

DWORD WINAPI my_thread(void* hModule) {
    ImGuiHook::Main(hModule);
    auto cocosBase = GetModuleHandleA("libcocos2d.dll");
    auto dispatchAddr = GetProcAddress(cocosBase, "?dispatchKeyboardMSG@CCKeyboardDispatcher@cocos2d@@QAE_NW4enumKeyCodes@2@_N@Z");
    MH_CreateHook(dispatchAddr, &dispatchKeyboardMSGHook, reinterpret_cast<void**>(&dispatchKeyboardMSG));
    MH_EnableHook(dispatchAddr);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        CreateThread(0, 0x1000, my_thread, hModule, 0, 0);
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

