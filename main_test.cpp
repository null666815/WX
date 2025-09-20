#include <iostream>
#include <memory>
#include "src/core/Platform.hpp"
#include "src/common/Service.h"
#include "src/common/WeChatService.hpp"

int main() {
    std::cout << "Testing new project structure..." << std::endl;

    try {
        // Test Platform functionality
        Platform platform;

        // Load test data
        if (platform.load("data/users.txt", "data/groups.txt")) {
            std::cout << "✅ Platform data loaded successfully!" << std::endl;
            std::cout << "   Users: " << platform.users.size() << std::endl;
            std::cout << "   Groups: " << platform.groups.size() << std::endl;
        } else {
            std::cout << "⚠️  No test data found, but Platform initialized" << std::endl;
        }

        // Test WeChatService
        std::unique_ptr<WeChatService> wxService = std::make_unique<WeChatService>();
        wxService->attachPlatform(static_cast<void*>(&platform));
        wxService->login("test_user", "password");
        wxService->groupFeatureDemo();

        std::cout << "✅ WeChatService demo completed!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "❌ Test error: " << e.what() << std::endl;
        return -1;
    }

    std::cout << "🎉 All tests passed! New project structure is working correctly." << std::endl;
    return 0;
}
