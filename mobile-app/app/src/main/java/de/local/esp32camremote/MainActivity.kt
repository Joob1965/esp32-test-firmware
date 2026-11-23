package de.local.esp32camremote

import android.app.Activity
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.webkit.WebResourceRequest
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.TextView

class MainActivity : Activity() {
    private lateinit var hostInput: EditText
    private lateinit var statusText: TextView
    private lateinit var webView: WebView
    private var cameraBaseUrl: String? = null
    private var showingSettings = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        buildScreen()
        hostInput.setText(
            getPreferences(MODE_PRIVATE).getString(PREF_CAMERA_HOST, "").orEmpty()
        )
    }

    private fun buildScreen() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(16), dp(12), dp(16), dp(8))
            setBackgroundColor(Color.WHITE)
        }

        val title = TextView(this).apply {
            text = "ESP32-CAM"
            textSize = 24f
            setTextColor(Color.rgb(25, 45, 70))
        }
        root.addView(title)

        hostInput = EditText(this).apply {
            hint = "Kamera-IP oder Hostname, z. B. 192.168.1.50"
            isSingleLine = true
            inputType = android.text.InputType.TYPE_CLASS_TEXT or
                android.text.InputType.TYPE_TEXT_VARIATION_URI
            imeOptions = EditorInfo.IME_ACTION_GO
            setTextColor(Color.BLACK)
            setHintTextColor(Color.DKGRAY)
            setOnEditorActionListener { _, actionId, _ ->
                if (actionId == EditorInfo.IME_ACTION_GO) {
                    showStream()
                    true
                } else {
                    false
                }
            }
        }
        root.addView(
            hostInput,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT
            ).apply {
                topMargin = dp(8)
                bottomMargin = dp(8)
            }
        )

        val actions = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
        }
        actions.addView(actionButton("Verbinden") { showStream() })
        actions.addView(actionButton("Einstellungen") { showSettings() })
        actions.addView(actionButton("Trennen") { disconnect() })
        root.addView(actions)

        statusText = TextView(this).apply {
            text = "Mit demselben WLAN wie die Kamera verbinden."
            textSize = 14f
            setTextColor(Color.DKGRAY)
            setPadding(0, dp(6), 0, dp(6))
        }
        root.addView(statusText)

        webView = WebView(this).apply {
            settings.javaScriptEnabled = true
            settings.domStorageEnabled = true
            settings.cacheMode = WebSettings.LOAD_NO_CACHE
            settings.allowFileAccess = false
            settings.allowContentAccess = false
            webViewClient = object : WebViewClient() {
                override fun onPageStarted(
                    view: WebView?,
                    url: String?,
                    favicon: android.graphics.Bitmap?
                ) {
                    statusText.text =
                        if (showingSettings) "Lade Kamera-Einstellungen …"
                        else "Verbinde mit dem Kamerastream …"
                }

                override fun onPageFinished(view: WebView?, url: String?) {
                    statusText.text =
                        if (showingSettings) "Einstellungen der Kamera"
                        else "Kamerastream: ${cameraBaseUrl.orEmpty()}"
                }

                override fun onReceivedError(
                    view: WebView?,
                    request: WebResourceRequest?,
                    error: android.webkit.WebResourceError?
                ) {
                    if (request?.isForMainFrame == true) {
                        statusText.text =
                            "Kamera nicht erreichbar. WLAN und IP-Adresse prüfen."
                    }
                }

                override fun shouldOverrideUrlLoading(
                    view: WebView?,
                    request: WebResourceRequest?
                ): Boolean {
                    val url = request?.url ?: return true
                    return url.scheme != "http" && url.scheme != "https"
                }
            }
            setBackgroundColor(Color.rgb(245, 247, 250))
        }
        root.addView(
            webView,
            LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                0,
                1f
            )
        )

        setContentView(root)
    }

    private fun actionButton(label: String, action: () -> Unit): Button =
        Button(this).apply {
            text = label
            isAllCaps = false
            setOnClickListener { action() }
            layoutParams = LinearLayout.LayoutParams(
                0,
                ViewGroup.LayoutParams.WRAP_CONTENT,
                1f
            )
        }

    private fun showStream() {
        val baseUrl = readCameraBaseUrl() ?: return
        cameraBaseUrl = baseUrl
        showingSettings = false
        saveCameraHost(baseUrl)
        webView.loadUrl("$baseUrl/stream")
    }

    private fun showSettings() {
        val baseUrl = cameraBaseUrl ?: readCameraBaseUrl() ?: return
        cameraBaseUrl = baseUrl
        showingSettings = true
        saveCameraHost(baseUrl)
        webView.loadUrl("$baseUrl:81/")
    }

    private fun readCameraBaseUrl(): String? {
        val entered = hostInput.text.toString().trim()
        val normalized = if (entered.contains("://")) entered else "http://$entered"
        val uri = Uri.parse(normalized)
        val host = uri.host
        if (uri.scheme != "http" || host.isNullOrBlank() ||
            uri.userInfo != null || uri.path.orEmpty() !in listOf("", "/") ||
            uri.query != null || uri.fragment != null
        ) {
            statusText.text = "Bitte eine gültige Kamera-IP oder einen Hostnamen eingeben."
            return null
        }
        if (uri.port != -1 && uri.port != 80) {
            statusText.text = "Die Kamera muss über HTTP-Port 80 erreichbar sein."
            return null
        }
        return "http://$host"
    }

    private fun saveCameraHost(baseUrl: String) {
        getPreferences(MODE_PRIVATE).edit()
            .putString(PREF_CAMERA_HOST, baseUrl.removePrefix("http://"))
            .apply()
    }

    private fun disconnect() {
        webView.stopLoading()
        webView.loadUrl("about:blank")
        cameraBaseUrl = null
        showingSettings = false
        statusText.text = "Getrennt. Kamera-IP oder Hostname eingeben."
    }

    private fun dp(value: Int): Int =
        (value * resources.displayMetrics.density).toInt()

    override fun onDestroy() {
        webView.destroy()
        super.onDestroy()
    }

    companion object {
        private const val PREF_CAMERA_HOST = "camera_host"
    }
}
