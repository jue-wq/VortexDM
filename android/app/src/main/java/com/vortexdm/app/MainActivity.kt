package com.vortexdm.app

import android.app.Activity
import android.os.Bundle
import android.view.ViewGroup
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoView

class MainActivity : Activity() {
    private lateinit var runtime: GeckoRuntime
    private lateinit var session: GeckoSession

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        runtime = GeckoRuntime.create(this)
        session = GeckoSession()
        session.open(runtime)
        val browserView = GeckoView(this)
        browserView.layoutParams = ViewGroup.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.MATCH_PARENT
        )
        browserView.setSession(session)
        setContentView(browserView)
        if (savedInstanceState == null) session.loadUri("https://example.com")
    }

    override fun onPause() {
        session.setActive(false)
        super.onPause()
    }

    override fun onResume() {
        super.onResume()
        session.setActive(true)
    }

    override fun onDestroy() {
        session.close()
        super.onDestroy()
    }
}
