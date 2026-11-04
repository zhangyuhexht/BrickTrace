package dev.bricktrace.gametest;

import net.fabricmc.fabric.api.client.gametest.v1.FabricClientGameTest;
import net.fabricmc.fabric.api.client.gametest.v1.context.ClientGameTestContext;
import net.fabricmc.fabric.api.client.gametest.v1.context.TestSingleplayerContext;

public class BrickTraceClientGameTest implements FabricClientGameTest {
    @Override
    public void runTest(ClientGameTestContext context) {
        try (TestSingleplayerContext world = context.worldBuilder().create()) {
            // Long enough for the mod to log camera/visible sections and to render RT frames.
            context.waitTicks(160);
            context.takeScreenshot("bricktrace_rt");
        }
    }
}
