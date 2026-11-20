package com.vortexdm.app

import android.app.Activity
import android.app.AlertDialog
import android.os.Bundle
import android.view.Gravity
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView
import android.util.Log
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoView
import org.mozilla.geckoview.WebExtension
import org.mozilla.geckoview.WebExtensionController

class MainActivity : Activity() {
    private lateinit var runtime: GeckoRuntime
    private lateinit var session: GeckoSession
    private lateinit var addressBar: EditText
    private lateinit var pageStatus: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        runtime = GeckoRuntime.create(this)
        configureExtensionPermissionPrompts()
        session = GeckoSession()
        session.open(runtime)

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(12, 12, 12, 0)
        }
        val addressRow = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        addressBar = EditText(this).apply {
            hint = "Search or enter address"
            singleLine = true
            imeOptions = EditorInfo.IME_ACTION_GO
            setOnEditorActionListener { _, actionId, _ ->
                if (actionId == EditorInfo.IME_ACTION_GO) {
                    navigate(addressBar.text.toString())
                    true
                } else false
            }
        }
        addressRow.addView(addressBar, LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f))
        addressRow.addView(Button(this).apply {
            text = "Go"
            setOnClickListener { navigate(addressBar.text.toString()) }
        })
        root.addView(addressRow)

        val controls = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER
        }
        controls.addView(Button(this).apply {
            text = "Back"
            setOnClickListener { if (session.isOpen) session.goBack() }
        })
        controls.addView(Button(this).apply {
            text = "Forward"
            setOnClickListener { if (session.isOpen) session.goForward() }
        })
        controls.addView(Button(this).apply {
            text = "Reload"
            setOnClickListener { if (session.isOpen) session.reload() }
        })
        controls.addView(Button(this).apply {
            text = "uBlock"
            contentDescription = "Install official uBlock Origin add-on"
            setOnClickListener { installOfficialUblockOrigin() }
        })
        root.addView(controls)

        pageStatus = TextView(this).apply {
            text = "Vortex Shield is initializing"
            textSize = 12f
        }
        root.addView(pageStatus)

        val browserView = GeckoView(this).apply {
            layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f)
            setSession(session)
        }
        root.addView(browserView)
        setContentView(root)

        installVortexShield()
        if (savedInstanceState == null) navigate("https://example.com")
    }

    private fun installVortexShield() {
        runtime.webExtensionController
            .ensureBuiltIn("resource://android/assets/vortex-shield/", "shield@vortexdm.app")
            .accept(
                { pageStatusOrLog("Vortex Shield active") },
                { error -> Log.e("VortexDM", "Shield installation failed", error) }
            )
    }

    private fun installOfficialUblockOrigin() {
        pageStatusOrLog("Requesting official uBlock Origin add-on")
        runtime.webExtensionController
            .install(
                "https://addons.mozilla.org/firefox/downloads/latest/ublock-origin/latest.xpi",
                WebExtensionController.INSTALLATION_METHOD_MANAGER
            )
            .accept(
                { extension -> pageStatusOrLog("uBlock Origin installed: ${extension.metaData.name}") },
                { error ->
                    Log.e("VortexDM", "uBlock Origin installation failed", error)
                    pageStatusOrLog("uBlock Origin could not be installed; see app log")
                }
            )
    }

    private fun configureExtensionPermissionPrompts() {
        runtime.webExtensionController.setPromptDelegate(
            object : WebExtensionController.PromptDelegate {
                override fun onInstallPromptRequest(
                    extension: WebExtension,
                    permissions: Array<String>,
                    origins: Array<String>,
                    dataCollectionPermissions: Array<String>
                ): GeckoResult<WebExtension.PermissionPromptResponse> {
                    val result = GeckoResult<WebExtension.PermissionPromptResponse>()
                    val requested = buildList {
                        addAll(permissions)
                        addAll(origins)
                        addAll(dataCollectionPermissions)
                    }.distinct().joinToString("\n")
                    AlertDialog.Builder(this@MainActivity)
                        .setTitle("Install ${extension.metaData.name}?")
                        .setMessage(
                            "This extension requests the following permissions:\n\n" +
                                requested + "\n\nOnly continue if you trust this add-on."
                        )
                        .setPositiveButton("Install") { _, _ ->
                            result.complete(
                                WebExtension.PermissionPromptResponse(true, false, false)
                            )
                        }
                        .setNegativeButton("Cancel") { _, _ ->
                            result.complete(
                                WebExtension.PermissionPromptResponse(false, false, false)
                            )
                        }
                        .setOnCancelListener {
                            result.complete(
                                WebExtension.PermissionPromptResponse(false, false, false)
                            )
                        }
                        .show()
                    return result
                }
            }
        )
    }

    private fun pageStatusOrLog(message: String) {
        runOnUiThread {
            if (::pageStatus.isInitialized) pageStatus.text = message
        }
    }

    private fun navigate(rawInput: String) {
        val input = rawInput.trim()
        if (input.isEmpty()) return
        val uri = when {
            input.startsWith("https://", true) || input.startsWith("http://", true) -> input
            input.contains(' ') -> "https://www.google.com/search?q=" + java.net.URLEncoder.encode(input, Charsets.UTF_8.name())
            input.contains('.') -> "https://$input"
            else -> "https://www.google.com/search?q=" + java.net.URLEncoder.encode(input, Charsets.UTF_8.name())
        }
        addressBar.setText(uri)
        session.loadUri(uri)
    }

    override fun onPause() {
        if (::session.isInitialized && session.isOpen) session.setActive(false)
        super.onPause()
    }

    override fun onResume() {
        super.onResume()
        if (::session.isInitialized && session.isOpen) session.setActive(true)
    }

    override fun onDestroy() {
        if (::session.isInitialized) session.close()
        super.onDestroy()
    }
}
