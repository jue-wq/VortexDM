package com.vortexdm.app

internal object NativeBridge {
    init { System.loadLibrary("vortex_android") }
    external fun coreVersion(): String
    external fun isSupportedUrl(url: String): Boolean
    external fun rangeHeader(first: Long, last: Long): String?
}
