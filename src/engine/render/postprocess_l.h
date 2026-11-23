#pragma once

class PostProcess_L {
public:
    ~PostProcess_L();

    bool init();
    void shutdown();

    // Allows disabling the HDR/bloom passes (e.g. on software renderers).
    void setEnabled(bool enabled) { m_userEnabled = enabled; }


    bool beginScene(int width, int height);


    void endScene(int outputWidth = 0, int outputHeight = 0);

    bool enabled() const { return m_ready && m_active; }

public:
    struct Impl;

private:
    Impl* m = nullptr;

    bool m_ready = false;
    bool m_active = false;
    bool m_userEnabled = true;
};
