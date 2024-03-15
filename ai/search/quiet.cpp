#include "quiet.h"

namespace Quiet
{

// 任意の列に drop 個ぷよを補充して消えるなら callback する
// その後、別の列に最大 2 個ずつ補充していくことによって、連鎖尾伸ばしの可能性を模索する（深さは depth まで）
void search(
    Field& field,
    i32 depth,
    i32 drop,
    std::function<void(Result)> callback
)
{
    u8 heights[6];
    field.get_heights(heights);

    // 左側の壁
    i8 x_min = 0;
    for (i8 i = 2; i >= 0; --i) {
        if (heights[i] > 11) {
            x_min = i + 1;
            break;
        }
    }

    // 右側の壁
    i8 x_max = 5;
    for (i8 i = 2; i < 6; ++i) {
        if (heights[i] > 11) {
            x_max = i - 1;
            break;
        }
    }

    Quiet::generate(
        field,
        x_min,
        x_max,
        -1,
        drop,
        // 列 x に 色 p のぷよを need 個補充したら消えた
        [&] (i8 x, i8 p, i32 need) {
            auto copy = field;

            for (i32 i = 0; i < need; ++i) {
                copy.data[p].set_bit(x, heights[x] + i);
            }

            auto sim = copy;
            auto sim_mask = sim.pop();
            auto sim_chain = Chain::get_score(sim_mask);

            // 2連鎖以上おきたら callback
            if (sim_chain.count > 1) {
                callback(Result {
                    .chain = sim_chain.count,
                    .score = sim_chain.score,
                    .x = x,
                    .depth = 0,
                    .plan = copy, // ぷよを置いたあとの盤面
                    .remain = sim // 連鎖したあとの盤面
                });
            }

            // ？
            if (depth < 2) {
                return;
            }

            u8 sim_heights[6];
            sim.get_heights(sim_heights);

            bool extendable = false;
            for (i32 i = x_min; i <= x_max; ++i) {
                // 今置いた列は飛ばす
                if (i == x) {
                    continue;
                }

                // ？
                if (heights[i] != sim_heights[i]) {
                    extendable = true;
                    break;
                }
            }

            if (!extendable) {
                return;
            }
            
            Quiet::dfs(
                sim,
                copy,
                x_min,
                x_max,
                x, // 今置いた列を ban してる
                sim_chain.count,
                depth - 1,
                callback
            );
        }
    );
};

void dfs(
    Field& field,
    Field& pre,
    i8 x_min,
    i8 x_max,
    i8 x_ban,
    i32 pre_count,
    i32 depth,
    std::function<void(Result)>& callback
)
{
    u8 pre_heights[6];
    pre.get_heights(pre_heights);

    Quiet::generate(
        field,
        x_min,
        x_max,
        x_ban,
        2, // 2個まで補充
        [&] (i8 x, i8 p, i32 need) {
            // 全部置こうとしたら窒息するので？
            if (i32(pre_heights[x]) + need + (x == 2) > 12) {
                return;
            }

            auto copy = pre;
            
            for (i32 i = 0; i < need; ++i) {
                copy.data[p].set_bit(x, pre_heights[x] + i);
            }

            // 先にこっちを置いても消えない <=> 先の連鎖とこれは独立でない <=> "伸ばし"に対応する？
            // x_ban でさっき置いた列に置くのを禁止しているので、これは特に連鎖尾伸ばし？
            // 
            // 例えば、1縦で赤を2個おいたら連鎖が起きて、6列目に2個青が残ったとする
            // このとき、赤を2個置く前に6列目に青を補充してみて、"それで何も消えなければ" 1連鎖伸びることになる
            // - 伸びる場合: https://www.puyop.com/s/8038030000000000000000000003gz9qkrkz
            // - 伸びない場合: https://www.puyop.com/s/8038030000000000000000000003gz9qjrkA
            if (copy.data[p].get_mask_group_4(x, pre_heights[x]).get_count() > 3) {
                return;
            }

            auto sim = copy;
            auto sim_mask = sim.pop();

            // 伸ばしに成功！
            if (sim_mask.get_size() > pre_count) {
                auto sim_chain = Chain::get_score(sim_mask);

                callback(Result {
                    .chain = sim_chain.count,
                    .score = sim_chain.score,
                    .x = x_ban,
                    .depth = depth,
                    .plan = copy,
                    .remain = sim
                });

                // 再帰
                if (depth > 1) {
                    Quiet::dfs(
                        sim,
                        copy,
                        x_min,
                        x_max,
                        x_ban,
                        sim_chain.count,
                        depth - 1,
                        callback
                    );
                }
            }
        }
    );
};

// すべての列 x (x_min <= x <= x_max, x != x_ban) に、最大 drop 個のぷよを縦に落とした結果
// なにか連鎖が起きたら callback
void generate(
    Field& field,
    i8 x_min,
    i8 x_max,
    i8 x_ban,
    i32 drop,
    std::function<void(i8, i8, i32)> callback
)
{
    u8 heights[6];
    field.get_heights(heights);

    for (i8 x = x_min; x <= x_max; ++x) {
        if (x == x_ban) {
            continue;
        }

        // 最大この列に何個落とせるか
        // x == 2 は窒息点があるので一個低める
        u8 drop_max = std::min(drop, 12 - heights[x] - (x == 2));

        if (drop_max == 0) {
            continue;
        }

        // 全色試す
        for (u8 p = 0; p < Cell::COUNT - 1; ++p) {
            auto copy = field;

            for (u8 i = 0; i < drop_max; ++i) {
                copy.data[p].set_bit(x, heights[x] + i);

                // 補充したぷよが何かと繋がって連鎖がおきたら callback
                if (copy.data[p].get_mask_group_4(x, heights[x]).get_count() >= 4) {
                    callback(x, p, i + 1);
                    break;
                }
            }
        }
    }
};

};