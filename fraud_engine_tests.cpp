#include "../src/fraud_engine.hpp"
#include <cassert>
#include <iostream>

using aurora::assess_transfer;

int main() {
    const auto safe = assess_transfer(1000, 50000, 1);
    assert(safe.status == "SAFE");
    assert(safe.score == 0);

    const auto review = assess_transfer(60000, 70000, 1);
    assert(review.status == "REVIEW");
    assert(review.score == 60);

    const auto block = assess_transfer(100000, 110000, 8);
    assert(block.status == "BLOCK");
    assert(block.score == 100);

    const auto invalid = assess_transfer(-1, 1000, 0);
    assert(invalid.status == "BLOCK");
    assert(invalid.score == 100);

    std::cout << "Fraud engine tests passed.\n";
    return 0;
}
