#include <visage.hpp>

float getDeltaTime() {
    static double lastFrame = 0.0;
    double time = glfwGetTime();
    float dt = static_cast<float>(time - lastFrame);
    lastFrame = time;

    return dt;
}

void handleKeyboardInput(visage::Visage& engine, bool& mouseLocked) {
    constexpr float movementSpeed = 3.0f;

    // only allow movement when mouse is locked
    if (mouseLocked) {
        float speed = movementSpeed * getDeltaTime();

        if (engine.isKeyHeld(GLFW_KEY_W)) engine.translateCamForward(-speed);
        if (engine.isKeyHeld(GLFW_KEY_S)) engine.translateCamForward(speed);
        if (engine.isKeyHeld(GLFW_KEY_A)) engine.translateCamRight(speed);
        if (engine.isKeyHeld(GLFW_KEY_D)) engine.translateCamRight(-speed);

        // up and down movement
        if (engine.isKeyHeld(GLFW_KEY_SPACE)) engine.translateCamVertically(-speed);
        if (engine.isKeyHeld(GLFW_KEY_LEFT_SHIFT)) engine.translateCamVertically(speed);
    }

    // lock/unlock mouse
    if (engine.isKeyReleased(GLFW_KEY_ESCAPE)) {
        mouseLocked = !mouseLocked;
        engine.lockMouse(mouseLocked);
    }

    // spawn object
    if (engine.isKeyReleased(GLFW_KEY_Q)) {
        engine.copyModel("glb_model.glb");
    }

    // spawn light
    if (engine.isKeyReleased(GLFW_KEY_E)) {
        engine.createLightAtCamera(5.0f);
    }

    // scene reset
    if (engine.isKeyReleased(GLFW_KEY_TAB)) {
        engine.resetScene();
    }
}

int main() {
    visage::Visage engine;

    // load models
    engine.loadModel("glb_model.glb", {-1.0f, 0.0f, 0.0f}, 5.0f);

    // load skybox
    engine.loadSkybox("kloppenheim_02_puresky.hdr");

    // create player following light
    engine.createPlayerLight(5.0f);

    // configure
    engine.setMouseSensitivity(2.0f);
    // engine.showDebugInfo();
    engine.enableRaytracing();

    // initialize engine
    engine.initialize();

    // lock mouse
    bool mouseLocked = true;
    engine.lockMouse(mouseLocked);

    // run engine
    while (engine.isRunning()) {
        handleKeyboardInput(engine, mouseLocked);
        engine.render();
    }

    return EXIT_SUCCESS;
}
