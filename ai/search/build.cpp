#include "build.h"

namespace Build
{

// 各スレッドで走らせるやつ
bool Thread::search(Field field, Cell::Queue queue, std::vector<Eval::Weight> w)
{
    if (this->thread != nullptr) {
        return false;
    }

    this->clear();

    // Type ごとに重みが違う（何特化のAIなのか）ので、それらすべてについて search を走らせる
    this->thread = new std::thread([&] (Field f, Cell::Queue q, std::vector<Eval::Weight> w_list) {
        for (auto& weight : w_list) {
            this->results.push_back(Build::search(f, q, weight));
        }
    }, field, queue, w);

    return true;
};

std::vector<Result> Thread::get()
{
    if (this->thread == nullptr) {
        return {};
    }

    this->thread->join();

    auto result = this->results;

    this->clear();

    return result;
};

void Thread::clear()
{
    if (this->thread != nullptr) {
        if (this->thread->joinable()) {
            this->thread->join();
        }
        delete this->thread;
    }

    this->thread = nullptr;
    this->results.clear();
};

Result search(Field field, std::vector<Cell::Pair> queue, Eval::Weight w, i32 thread_count)
{
    // 2手の全探索を基本としているので、それ以下なら帰る
    if (queue.size() < 2) {
        return Result();
    }

    Result result = Result();

    Node root = Node {
        .field = field,
        .tear = 0, // ちぎった段数の合計
        .waste = 0 // 途中で起こった連鎖数の合計、あくまでも構築用のAIなので起こった連鎖はすべて無駄消し判定してるっぽい
    };

    // 次のツモがどこにおけるかの avec
    auto placements = Move::generate(field, queue[0].first == queue[0].second);

    std::mutex mtx;
    std::vector<std::thread> threads;

    for (i32 t = 0; t < thread_count; ++t) {
        threads.emplace_back([&] () {
            while (true)
            {
                Move::Placement placement;

                {
                    std::lock_guard<std::mutex> lk(mtx);
                    // もう試すべき置き場所がないならおわり
                    if (placements.get_size() < 1) {
                        break;
                    }
                    placement = placements[placements.get_size() - 1];
                    placements.pop();
                }

                Candidate candidate = Candidate {
                    .placement = placement,
                    .eval = Eval::Result(),
                    .eval_fast = INT32_MIN
                };

                auto child = root;

                // ツモを置く
                child.field.drop_pair(placement.x, placement.r, queue[0]);
                // なにか消えるなら消す
                auto mask_pop = child.field.pop();

                // 窒息
                if (child.field.get_height(2) > 11) {
                    continue;
                }

                // ちぎりの段数
                child.tear += root.field.get_drop_pair_frame(placement.x, placement.r) - 1;
                // 起こった連鎖数
                child.waste += mask_pop.get_size();

                // 次ツモを置いた時点での評価点（ネクスト以降は未考慮）
                candidate.eval_fast = Eval::evaluate(child.field, child.tear, child.waste, w).value;

                candidate.eval = Build::dfs(child, queue, w, 1);

                if (candidate.eval.value == INT32_MIN) {
                    continue;
                }

                {
                    std::lock_guard<std::mutex> lk(mtx);
                    // とりあえず死なないなら候補として push_back
                    result.candidates.push_back(std::move(candidate));
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    return result;
};

// queue[depth] のツモを置く
Eval::Result dfs(Node& node, std::vector<Cell::Pair>& queue, Eval::Weight& w, i32 depth)
{
    auto result = Eval::Result();

    auto placements = Move::generate(node.field, queue[depth].first == queue[depth].second);

    for (i32 i = 0; i < placements.get_size(); ++i) {
        auto child = node;
        child.field.drop_pair(placements[i].x, placements[i].r, queue[depth]);
        auto mask_pop = child.field.pop();

        if (child.field.get_height(2) > 11) {
            continue;
        }

        child.tear += node.field.get_drop_pair_frame(placements[i].x, placements[i].r) - 1;
        child.waste += mask_pop.get_size();

        Eval::Result eval = Eval::Result();

        // 葉じゃないなら再帰
        if (depth + 1 < queue.size()) {
            eval = Build::dfs(child, queue, w, depth + 1);
        }
        // 葉ならここでの評価を代入
        else {
            eval = Eval::evaluate(child.field, child.tear, child.waste, w);
        }

        if (eval.value > result.value) {
            result.value = eval.value;
            result.plan = eval.plan;
        }

        if (eval.q > result.q) {
            result.q = eval.q;
        }
    }

    return result;
};

};