#pragma once

#include <GLFW/glfw3.h>

struct MouseObject {
    bool locked = true;

    float lastX = 0.0f;
    float lastY = 0.0f;

    float upAngle = 0.0f;
    float rightAngle = 0.0f;

    float sensitivity = 3.0f;
};

class MouseSingleton {
private:
    MouseObject mouse{};

    MouseSingleton() = default;

    // delete copy and move
    MouseSingleton(const MouseSingleton&) = delete;
    MouseSingleton& operator=(const MouseSingleton&) = delete;
    MouseSingleton(MouseSingleton&&) = delete;
    MouseSingleton& operator=(MouseSingleton&&) = delete;

public:
    [[nodiscard]] static MouseSingleton& v() noexcept {
        static MouseSingleton i;
        return i;
    }

    [[nodiscard]] MouseObject* getMouse() noexcept { return &mouse; }
};

static void mouseCallback(GLFWwindow* window, double xPos, double yPos) noexcept {
    float xp = static_cast<float>(xPos);
    float yp = static_cast<float>(yPos);

    MouseObject* mouse = MouseSingleton::v().getMouse();

    if (mouse->locked) {
        float xoff = mouse->lastX - xp;
        float yoff = mouse->lastY - yp;
        mouse->lastX = xp;
        mouse->lastY = yp;

        float sens = mouse->sensitivity;
        xoff *= sens;
        yoff *= sens;

        mouse->rightAngle -= xoff;
        mouse->upAngle -= yoff;
    }
}