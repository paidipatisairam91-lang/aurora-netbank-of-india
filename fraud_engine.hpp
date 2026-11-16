#pragma once
#include "json.hpp"

namespace aurora {

struct FraudAssessment {
    int score = 0;
    std::string status = "SAFE";
    std::string reason = "Normal transaction pattern";
    Json to_json() const;
};

FraudAssessment assess_transfer(double amount, double available_balance, int recent_transactions);

}
