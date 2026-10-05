#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

namespace UGT {

static constexpr double DEFAULT_TPS = 240.0;

struct PrecisionClock {
    double ticks = 0.0;
    double remainder = 0.0;
    uint64_t wholeTicks = 0;

    void reset() {
        ticks = 0.0;
        remainder = 0.0;
        wholeTicks = 0;
    }

    void advance(float dt, double tps) {
        if (dt <= 0.f || tps <= 0.0)
            return;

        // Accumulate in double precision so long runs do not repeatedly
        // round a float-sized frame accumulator.
        double add = static_cast<double>(dt) * tps;
        remainder += add;

        auto whole = static_cast<uint64_t>(remainder);
        if (whole != 0) {
            wholeTicks += whole;
            remainder -= static_cast<double>(whole);
        }

        ticks = static_cast<double>(wholeTicks) + remainder;
    }
};

static PrecisionClock clock;
static uint64_t lastInputTick = 0;
static bool haveInput = false;
static bool boundarySoundArmed = true;
static cocos2d::CCLabelBMFont* hud = nullptr;

static double tps() {
    auto value = Mod::get()->getSettingValue<int64_t>("tps");
    if (value < 60)
        value = 60;
    if (value > 2400)
        value = 2400;
    return static_cast<double>(value);
}

static int windowSize() {
    auto value = Mod::get()->getSettingValue<int64_t>("window-size");
    return static_cast<int>(std::clamp<int64_t>(value, 1, 2400));
}

static void resetClock() {
    clock.reset();
    lastInputTick = 0;
    haveInput = false;
    boundarySoundArmed = true;
}

static void ensureHUD(cocos2d::CCNode* parent) {
    if (hud && hud->getParent())
        return;

    hud = cocos2d::CCLabelBMFont::create("UGT 0", "bigFont.fnt");
    if (!hud)
        return;

    hud->setID("ugt-frame-hud"_spr);
    hud->setScale(0.34f);
    hud->setAnchorPoint({0.f, 1.f});
    hud->setZOrder(9999);

    auto size = cocos2d::CCDirector::sharedDirector()->getWinSize();
    hud->setPosition({8.f, size.height - 8.f});
    parent->addChild(hud);
}

static void updateHUD(cocos2d::CCNode* parent) {
    if (!Mod::get()->getSettingValue<bool>("show-hud")) {
        if (hud)
            hud->setVisible(false);
        return;
    }

    ensureHUD(parent);
    if (!hud)
        return;

    hud->setVisible(true);

    auto now = clock.wholeTicks;
    auto window = static_cast<uint64_t>(windowSize());

    uint64_t sinceInput = haveInput && now >= lastInputTick
        ? now - lastInputTick
        : 0;

    std::string text = fmt::format(
        "UGT  |  TICK {:d}  |  INPUT {:s}  |  Δ {:d}  |  WIN {:d}",
        now,
        haveInput ? std::to_string(lastInputTick) : "-",
        sinceInput,
        window
    );

    hud->setString(text.c_str());

    bool atBoundary = haveInput && sinceInput == window;

    if (atBoundary && boundarySoundArmed &&
        Mod::get()->getSettingValue<bool>("sound-enabled")) {

        auto path = (Mod::get()->getResourcesDir() / "sounds" / "window.ogg").string();

        FMODAudioEngine::sharedEngine()->playEffect(
            gd::string(path),
            1.f,
            0.f,
            static_cast<float>(
                Mod::get()->getSettingValue<double>("sound-volume")
            )
        );

        boundarySoundArmed = false;
    }

    if (sinceInput != window)
        boundarySoundArmed = true;
}

} // namespace UGT

class $modify(UGTPlayLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        UGT::resetClock();
    }

    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        UGT::clock.advance(dt, UGT::tps());
        UGT::updateHUD(this);
    }
};

class $modify(UGTInputLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool player1) {
        // Record the timing position of every real input event. This is
        // intentionally separate from the rendered FPS.
        if (!down)
            goto forward;

        UGT::lastInputTick = UGT::clock.wholeTicks;
        UGT::haveInput = true;
        UGT::boundarySoundArmed = true;

    forward:
        GJBaseGameLayer::handleButton(down, button, player1);
    }
};

class $modify(UGTPauseLayer, PauseLayer) {
    bool init(bool fromRestart) {
        if (!PauseLayer::init(fromRestart))
            return false;

        auto label = cocos2d::CCLabelBMFont::create(
            "UGT v0.1.0", "bigFont.fnt"
        );

        if (label) {
            label->setScale(0.28f);
            label->setOpacity(150);

            auto size = cocos2d::CCDirector::sharedDirector()->getWinSize();
            label->setPosition({size.width - 55.f, 18.f});

            this->addChild(label);
        }

        return true;
    }
};
