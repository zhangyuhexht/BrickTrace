package dev.bricktrace.client.mixin;

import dev.bricktrace.client.rt.BrickTraceDirtySections;
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.LevelChunk;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

@Mixin(LevelChunk.class)
public abstract class LevelChunkMixin {
    @Inject(method = "setBlockState", at = @At("RETURN"))
    private void bricktrace$markDirty(BlockPos pos, BlockState state, int flags,
                                      CallbackInfoReturnable<BlockState> cir) {
        if (cir.getReturnValue() == null) {
            return;
        }
        BrickTraceDirtySections.mark(pos.getX() >> 4, pos.getY() >> 4, pos.getZ() >> 4);
    }
}
