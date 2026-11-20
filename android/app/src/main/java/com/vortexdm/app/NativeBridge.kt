package com.vortexdm.app

internal object NativeBridge {
    init { System.loadLibrary("vortex_android") }
    external fun coreVersion(): String
}
