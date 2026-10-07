// 机制模块（mechanics/*.cpp）的 ctx 方法块注册汇总。
// 新增机制：建立 mechanics/<name>.cpp，定义 `void <name>_ctx(CtxTypes&)`，
// 并在下方列表加一行。框架函数（Engine::xxx）同文件放置。
#include "engine/effect_ctx.hpp"

namespace fy {
namespace mechanics {

// 声明：各机制源文件提供。内容为该机制专属的 ctx/attack 绑定块
// （从 effect_host.cpp 迁出，见 git 历史）。
// ---- 当前迁移进度：Wave 1（蒸汽/冰晶/裂伤）---------------------------
void steam_ctx(CtxTypes& t);   // 11-Thallya
void ice_ctx(CtxTypes& t);     // 15-Konuru
void wound_ctx(CtxTypes& t);   // 24-Shisui
void market_ctx(CtxTypes& t);  // 23-Akina
void soil_ctx(CtxTypes& t);    // 19-Megumi
void fate_ctx(CtxTypes& t);    // 26-Innealra
void drama_ctx(CtxTypes& t);   // 20-Kanawe
void dive_ctx(CtxTypes& t);    // 17-Hatsumi
void barracks_ctx(CtxTypes& t);// 18-Mizuki
void curse_ctx(CtxTypes& t);   // 21-Kamuwi

}  // namespace mechanics

void run_mechanic_ctx_blocks(CtxTypes& t) {
  mechanics::steam_ctx(t);
  mechanics::ice_ctx(t);
  mechanics::wound_ctx(t);
  mechanics::market_ctx(t);
  mechanics::soil_ctx(t);
  mechanics::fate_ctx(t);
  mechanics::drama_ctx(t);
  mechanics::dive_ctx(t);
  mechanics::barracks_ctx(t);
  mechanics::curse_ctx(t);
}

}  // namespace fy
