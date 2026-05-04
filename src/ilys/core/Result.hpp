#pragma once

#include <string>
#include <utility>

namespace ilys::core {

struct Result {
    bool ok{true};
    std::string message{};

    static Result success(std::string message = {})
    {
        return Result{true, std::move(message)};
    }

    static Result failure(std::string message)
    {
        return Result{false, std::move(message)};
    }
};

} // namespace ilys::core
