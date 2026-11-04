package dev.bricktrace.client.rt;

import net.minecraft.client.gui.components.Button;
import net.minecraft.client.gui.screens.Screen;
import net.minecraft.network.chat.Component;

/** RTX settings page. DLSS is listed but disabled: it is not implemented yet. */
public final class BrickTraceSettingsScreen extends Screen {
    private static final String[] DEBUG_NAMES = {"Shaded", "Shadow term", "Normals"};

    public BrickTraceSettingsScreen() {
        super(Component.literal("BrickTrace — RTX"));
    }

    @Override
    protected void init() {
        final int centerX = this.width / 2 - 110;
        int y = this.height / 2 - 80;

        addRenderableWidget(Button.builder(
                Component.literal("Ray tracing: " + onOff(BrickTraceSettings.rayTracing)),
                button -> {
                    BrickTraceSettings.rayTracing = !BrickTraceSettings.rayTracing;
                    button.setMessage(Component.literal(
                            "Ray tracing: " + onOff(BrickTraceSettings.rayTracing)));
                    BrickTraceSettings.save();
                }).bounds(centerX, y, 220, 20).build());
        y += 24;

        addRenderableWidget(Button.builder(
                Component.literal("RT shadows: " + onOff(BrickTraceSettings.shadows)),
                button -> {
                    BrickTraceSettings.shadows = !BrickTraceSettings.shadows;
                    button.setMessage(Component.literal(
                            "RT shadows: " + onOff(BrickTraceSettings.shadows)));
                    BrickTraceSettings.save();
                    BrickTraceRtView.applySettingsToNative();
                }).bounds(centerX, y, 220, 20).build());
        y += 24;

        addRenderableWidget(Button.builder(
                Component.literal("Debug view: " + DEBUG_NAMES[BrickTraceSettings.debugMode]),
                button -> {
                    BrickTraceSettings.debugMode = (BrickTraceSettings.debugMode + 1) % 3;
                    button.setMessage(Component.literal(
                            "Debug view: " + DEBUG_NAMES[BrickTraceSettings.debugMode]));
                    BrickTraceSettings.save();
                    BrickTraceRtView.applySettingsToNative();
                }).bounds(centerX, y, 220, 20).build());
        y += 32;

        final Button dlss = Button.builder(
                Component.literal("DLSS / DLSS RR: not implemented"), button -> {
                }).bounds(centerX, y, 220, 20).build();
        dlss.active = false;
        addRenderableWidget(dlss);
        y += 24;

        addRenderableWidget(Button.builder(Component.literal("Done"), button -> onClose())
                .bounds(centerX, y, 220, 20).build());
    }

    private static String onOff(boolean value) {
        return value ? "ON" : "OFF";
    }

    @Override
    public boolean isPauseScreen() {
        return false;
    }
}
