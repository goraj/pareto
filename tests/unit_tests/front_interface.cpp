

#include "../test_helpers.h"
#include <catch2/catch_all.hpp>

TEST_CASE("Front Interface") {
    SECTION("Front 2d") {
        /*
         * A two-dimensional front is probably the most common
         * type of front.
         */
        using namespace pareto;
        front<double, 2, unsigned> pf;
        pf(0.32, 0.75) = 3;
        pf(.7, 0.70) = 4;
        for (const auto &[p, v] : pf) {
            std::cout << "p: " << p << std::endl;
        }

        using satisfies_t = satisfies<double, 2, unsigned>;
        auto two_plus = [](const front<double, 2, unsigned>::value_type &v) {
            return v.second > 2;
        };
        auto condition_a = satisfies_t(two_plus);
        auto condition_b =
            disjoint(front<double, 2, unsigned>::box_type({0., 0.}, {.5, .7}));
        auto condition_c =
            intersects(front<double, 2, unsigned>::box_type({.4, .4}, {1.5, 1.5}));
        auto it = pf.find({condition_a, condition_b, condition_c});

        while (it != pf.end()) {
            std::cout << "it->first: " << it->first << std::endl;
            ++it;
        }
    }

    SECTION("Front 1d") {
        /*
         * 1-dimensional fronts are useful for applications
         * that might solve the problem with any arbitrary
         * number of objectives. They are also useful if these
         * applications still need 1-d archives to keep their
         * best solution.
         */
        using namespace pareto;
        using front_type = front<double, 1, unsigned>;
        front_type pf;
        pf(3.) = 0;
        pf(4.) = 0;
        pf(2.) = 0;
        pf(1.) = 0;
        for (const auto &[p, v] : pf) {
            std::cout << "p: " << p << std::endl;
        }
    }

    SECTION("Runtime size") {
        /*
         * Runtime size is useful when we don't know
         * the number of dimensions before the application
         * starts. This might be the case for our python
         * bindings, for instance. This, however,
         * is very inefficient because it requires
         * extra memory allocations, while constant dimensions
         * can allocate everything on the stack and use
         * a memory pool for nodes.
         */
        using namespace pareto;
        using front_type = front<double, 0, unsigned>;
        front_type pf;
        pf(3.) = 0;
        pf(4.) = 0;
        pf(2.) = 0;
        pf(1.) = 0;
        for (const auto &[p, v] : pf) {
            std::cout << "p: " << p << std::endl;
        }
    }

    SECTION("Integer Front") {
        /*
         * Runtime size is useful when we don't know
         * the number of dimensions before the application
         * starts. This might be the case for our python
         * bindings, for instance. This, however,
         * is very inefficient because it requires
         * extra memory allocations, while constant dimensions
         * can allocate everything on the stack and use
         * a memory pool for nodes.
         */
        using namespace pareto;
        using front_type = front<int, 0, int>;
        front_type pf;
        pf(3, 4) = 0;
        pf(4, 6) = 0;
        pf(2, 7) = 0;
        pf(1, 9) = 0;
        for (const auto &[p, v] : pf) {
            std::cout << "p: " << p << std::endl;
        }
        REQUIRE(pf.hypervolume() != 0);
    }
}