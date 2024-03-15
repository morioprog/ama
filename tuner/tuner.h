#pragma once

#include "score.h"

namespace Tuner
{

struct SaveData {
    Eval::Weight w;
    i32 count;
    i32 frame;
    i32 score;
    i32 unchange;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SaveData, w, count, frame, score, unchange)

void save(std::string id, SaveData s)
{
    std::string name = std::string("data/") + id + std::string(".json");
    std::ofstream o(name);
    json js;
    to_json(js, s);
    o << std::setw(4) << js << std::endl;
    o.close();
};

void load(std::string id, SaveData& s)
{
    std::string name = std::string("data/") + id + std::string(".json");
    std::ifstream file;
    file.open(name);
    json js;
    file >> js;
    file.close();
    from_json(js, s);
};

// 値が範囲外に行かないようにするやつ
Eval::Weight constrain(Eval::Weight w)
{
    #define CONSTRAIN_POSITIVE(p) w.p = std::max(0, w.p);
    #define CONSTRAIN_NEGATIVE(p) w.p = std::min(0, w.p);

    CONSTRAIN_POSITIVE(y);
    CONSTRAIN_POSITIVE(link_h);

    CONSTRAIN_NEGATIVE(key);
    CONSTRAIN_NEGATIVE(key_s);
    CONSTRAIN_NEGATIVE(need);
    CONSTRAIN_NEGATIVE(shape);
    CONSTRAIN_NEGATIVE(tear);
    CONSTRAIN_NEGATIVE(waste);

    return w;
};

// SPSA の次の候補を出すやつ
// w: 現在の重み
// idx: どの値を動かすかを指定したかったらここに渡す
std::pair<Eval::Weight, Eval::Weight> randomize(Eval::Weight w, std::optional<i32> idx = {})
{
    auto w1 = w;
    auto w2 = w;

    #define PARAM_COUNT 8

    i32 rng = idx.value_or(rand() % PARAM_COUNT);

    // チューニングしたいフィールドに対するポインタ
    i32* param_ptr[] = {
        &w.y,
        &w.key,
        &w.key_s,
        &w.need,
        &w.link_h,
        &w.shape,
        &w.tear,
        &w.waste
    };

    auto param_count = sizeof(*__countof_helper(param_ptr));

    auto param = param_ptr[rng];
    auto param_pre = *param;

    // `need` だけ 10 未満にしてる
    auto delta = 20;
    if (rng == 3) delta = 10;

    // 値は最低2動かす
    auto value = 2 + (rand() % delta);

    // w1: 正方向に動かしたもの
    *param += value;
    w1 = constrain(w);

    // w2: 負方向に動かしたもの
    *param = param_pre;
    *param -= value;
    w2 = constrain(w);

    return { w1, w2 };
};

i32 match(Eval::Weight w, Eval::Weight w1, Eval::Weight w2, i32 result[9])
{
    std::atomic<i32> count = 0;
    std::atomic<i32> count1 = 0;
    std::atomic<i32> count2 = 0;

    std::atomic<i32> frame = 0;
    std::atomic<i32> frame1 = 0;
    std::atomic<i32> frame2 = 0;

    std::atomic<i32> score = 0;
    std::atomic<i32> score1 = 0;
    std::atomic<i32> score2 = 0;

    // ツモ？
    std::vector<Cell::Pair> queues[100];
    for (i32 i = 0; i < 100; ++i) {
        queues[i] = Cell::create_queue(rand() & 0xFFFF);
    }

    std::vector<std::thread> threads;

    std::atomic<i32> progress = 0;

    // 4スレッドでとこぷよを回しまくってるっぽい
    for (i32 t = 0; t < 4; ++t) {
        threads.emplace_back([&] (i32 tid) {
            for (i32 i = 0; i < 25; ++i) {
                auto queue = queues[tid * 25 + i];

                auto sim = simulate(w, queue);
                auto sim1 = simulate(w1, queue);
                auto sim2 = simulate(w2, queue);

                if (sim.first >= AI::TRIGGER_DEFAULT) {
                    count += 1;
                    frame += sim.second;
                    score += sim.first;
                }

                if (sim1.first >= AI::TRIGGER_DEFAULT) {
                    count1 += 1;
                    frame1 += sim1.second;
                    score1 += sim1.first;
                }

                if (sim2.first >= AI::TRIGGER_DEFAULT) {
                    count2 += 1;
                    frame2 += sim2.second;
                    score2 += sim2.first;
                }

                progress += 1;
                printf("\rprogress: %d", progress.load());
            }
        }, t);
    }

    for (auto& thread : threads) {
        thread.join();
    }

    printf("\n");

    struct Score {
        i32 count = 0;
        i32 frame = 0;
        i32 score = 0;

        bool operator < (Score other)  {
            if (this->count != other.count) {
                return this->count < other.count;
            }

            if (this->frame != other.frame) {
                return this->frame > other.frame;
            }

            return this->score < other.score;
        };
    };

    Score s = { .count = count.load(), .frame = frame.load(), .score = score.load() };
    Score s1 = { .count = count1.load(), .frame = frame1.load(), .score = score1.load() };
    Score s2 = { .count = count2.load(), .frame = frame2.load(), .score = score2.load() };

    result[0] = count.load();
    result[1] = frame.load();
    result[2] = score.load();

    result[3] = count1.load();
    result[4] = frame1.load();
    result[5] = score1.load();

    result[6] = count2.load();
    result[7] = frame2.load();
    result[8] = score2.load();

    // 0: w
    // 1: w1
    // -1: w2

    if (s < s1 || s < s2) {
        if (s1 < s2) {
            return -1;
        }

        return 1;
    }
    
    return 0;
};

static void print_w(Eval::Weight w)
{
    #define PRW(p) printf("%s: %d\n", #p, w.p);

    PRW(y);
    PRW(key);
    PRW(key_s);
    PRW(need);
    PRW(link_h);
    PRW(shape);
    PRW(tear);
    PRW(waste);
};

static void run(Eval::Weight w)
{
    i32 unchange = 0;

    i32 id = 0;

    i32 pidx = 0;

    while (true)
    {
        auto randw = Tuner::randomize(w, pidx);

        // 各パラメータを順繰り更新していく
        pidx += 1;
        pidx = pidx % PARAM_COUNT;

        auto w1 = randw.first;
        auto w2 = randw.second;

        i32 result[9] = { 0 };

        auto m = Tuner::match(w, w1, w2, result);

        system("cls");
        printf("id: %d\n\n", id);
        printf("w0: %d - %d - %d\n", result[0], result[1] / result[0], result[2] / result[0]);
        printf("w+: %d - %d - %d\n", result[3], result[4] / result[3], result[5] / result[3]);
        printf("w-: %d - %d - %d\n", result[6], result[7] / result[6], result[8] / result[6]);
        printf("\n");
        Tuner::print_w(w);

        if (m == 1) {
            unchange = 0;
            w = w1;
        }
        else if (m == -1) {
            unchange = 0;
            w = w2;
        }
        else {
            unchange += 1;

            // 全パラメータが更新されなくなったらおわり（到達することあるのかな...？）
            if (unchange >= PARAM_COUNT) {
                break;
            }

            if (id > 0) {
                SaveData save_data;

                Tuner::load(std::to_string(id - 1), save_data);
                save_data.unchange = unchange;
                Tuner::save(std::to_string(id - 1), save_data);
            }

            continue;
        }

        // 以下の処理は、重みが更新された場合のみ（w -> w1 or w2）

        auto save_data = SaveData{
            .w = w, // 重み
            .count = 0, // 80000点以上が何回打てたか
            .frame = 0, // フレーム数の平均（打てた場合のみ）
            .score = 0, // 点数の平均（打てた場合のみ）
            .unchange = 0, // 連続何回更新されていないか
        };

        if (m == 1) {
            save_data.count = result[3];
            save_data.frame = result[4] / result[3];
            save_data.score = result[5] / result[3];
        }

        if (m == -1) {
            save_data.count = result[6];
            save_data.frame = result[7] / result[6];
            save_data.score = result[8] / result[6];
        }

        Tuner::save(std::to_string(id), save_data);

        id += 1;
    }
};

};