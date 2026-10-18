#pragma once
#include <cfloat>
#include <cstdint>
typedef uint32_t ImU32;
struct ImVec2 { float x, y; ImVec2() : x(0), y(0) {} ImVec2(float a, float b) : x(a), y(b) {} };
#define IM_COL32(R,G,B,A) (((ImU32)(A)<<24)|((ImU32)(B)<<16)|((ImU32)(G)<<8)|((ImU32)(R)))
#define IMGUI_CHECKVERSION()
enum ImGuiConfigFlags_ { ImGuiConfigFlags_NoMouseCursorChange = 1 << 4 };
enum ImGuiCond_ { ImGuiCond_FirstUseEver = 4 };
enum ImGuiWindowFlags_ { ImGuiWindowFlags_NoCollapse = 1 << 5 };
typedef int ImGuiCond, ImGuiWindowFlags;
struct ImDrawList { void AddRectFilled(const ImVec2&, const ImVec2&, ImU32, float = 0); void AddText(const ImVec2&, ImU32, const char*); };
struct ImDrawData;
struct ImGuiStyle { void ScaleAllSizes(float); };
struct ImGuiIO { const char* IniFilename; int ConfigFlags; float FontGlobalScale; ImVec2 DisplaySize; float DeltaTime;
  void AddMousePosEvent(float, float); void AddMouseButtonEvent(int, bool); };
namespace ImGui {
void* CreateContext(); ImGuiIO& GetIO(); void StyleColorsDark(); ImGuiStyle& GetStyle();
ImDrawList* GetBackgroundDrawList(); float GetFontSize(); ImVec2 CalcTextSize(const char*);
void SetNextWindowSize(const ImVec2&, ImGuiCond = 0); void SetNextWindowPos(const ImVec2&, ImGuiCond = 0, const ImVec2& = ImVec2());
bool Begin(const char*, bool* = nullptr, ImGuiWindowFlags = 0); void End();
ImVec2 GetWindowPos(); ImVec2 GetWindowSize(); void SeparatorText(const char*);
bool Checkbox(const char*, bool*); bool SliderFloat(const char*, float*, float, float, const char* = "%.3f");
void Spacing(); bool Button(const char*); void NewFrame(); void Render(); ImDrawData* GetDrawData();
}
