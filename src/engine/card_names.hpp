#pragma once
// Names of cards whose behaviour the engine has to special-case. Centralising
// them means a content rename is caught by `content names referenced by the
// engine all exist` (rules_matrix_tests.cpp) instead of silently disabling an
// entire mechanic.
//
// Prefer adding a content flag / hook over adding a new name here.
namespace fy {
namespace cards {

// Oboro
inline constexpr const char* kXuYu = "虚鱼";            // reverse crystal moves / extra setup
inline constexpr const char* kNinbu = "忍步";           // extra setup card when used as 设置
inline constexpr const char* kShinraedashi = "神代枝";  // (documented, handled in content)
inline constexpr const char* kSaigoNoKesshou = "最后的结晶";  // revive window is engine-driven
// Thallya
inline constexpr const char* kYasha = "夜叉";
inline constexpr const char* kAshura = "阿修罗";
inline constexpr const char* kSariaNoKessaku = "萨利亚的杰作";
inline constexpr const char* kSokaiKaisou = "快速改装";
inline constexpr const char* kRenseiKougeki = "炼成攻击";
// Chikage
inline constexpr const char* kMetsutouDoku = "灭灯毒";
inline constexpr const char* kChikanDoku = "迟缓毒";
// Hagane / Kururu
inline constexpr const char* kMud = "泥泞";  // Chikage: blocks 后退/离脱
inline constexpr const char* kHackDevice = "枢的骇客装置";

}  // namespace cards
}  // namespace fy
