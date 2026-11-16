#include "fraud_engine.hpp"
#include <algorithm>

namespace aurora {

Json FraudAssessment::to_json() const {
    Json result = Json::object();
    result["score"] = score;
    result["status"] = status;
    result["reason"] = reason;
    return result;
}

FraudAssessment assess_transfer(double amount, double available_balance, int recent_transactions) {
    FraudAssessment assessment;
    if (amount <= 0 || available_balance < 0 || recent_transactions < 0) {
        assessment.score = 100;
        assessment.status = "BLOCK";
        assessment.reason = "Invalid transaction values";
        return assessment;
    }

    if (amount >= 100000) {
        assessment.score += 60;
        assessment.reason = "Very high transaction amount";
    } else if (amount >= 50000) {
        assessment.score += 35;
        assessment.reason = "High transaction amount";
    }

    if (available_balance > 0 && amount >= available_balance * 0.80) {
        assessment.score += 25;
        assessment.reason = "Transfer uses at least 80 percent of available balance";
    }

    if (recent_transactions >= 8) {
        assessment.score += 25;
        assessment.reason = "Unusual transaction frequency";
    } else if (recent_transactions >= 5) {
        assessment.score += 15;
        assessment.reason = "Elevated transaction frequency";
    }

    assessment.score = std::min(assessment.score, 100);
    assessment.status = assessment.score >= 80 ? "BLOCK" :
                        assessment.score >= 40 ? "REVIEW" : "SAFE";
    return assessment;
}

}
