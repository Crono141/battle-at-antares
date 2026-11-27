/* antares-gog-signin (Linux): GOG's own sign-in page in a window (WP5.13b, D29 case 3).
 *
 * The player types their password into GOG's page, never into Battle at Antares. When GOG sends
 * the browser to https://embed.gog.com/on_login_success?...&code=..., this program writes the
 * one-time code to standard output and exits 0; closing the window exits 1. The session is
 * ephemeral (no cookies, no cache, nothing on disk), so nothing outlives the window, and no script
 * is injected into the page: the redirect is seen through WebKit's own navigation policy signal.
 * It is a separate program so the launcher starts and works on systems without WebKitGTK. */
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <webkit2/webkit2.h>

#define GOG_AUTH_URL                                                                                                                      \
    "https://auth.gog.com/auth?client_id=46899977096215655&redirect_uri=https%3A%2F%2Fembed.gog.com%2Fon_login_success%3Forigin%3Dclient" \
    "&response_type=code&layout=client2"

static int s_exit_code = 1;

/* The code from the success address, or NULL when the address isn't GOG's success page. */
static gchar* Code_From_(const gchar* uri) {
    GUri* parsed = g_uri_parse(uri, G_URI_FLAGS_NONE, NULL);
    gchar* code = NULL;
    if (parsed == NULL) return NULL;
    const gchar* host = g_uri_get_host(parsed);
    const gchar* query = g_uri_get_query(parsed);
    if (g_strcmp0(g_uri_get_scheme(parsed), "https") == 0 && host != NULL && g_ascii_strcasecmp(host, "embed.gog.com") == 0 &&
        g_strcmp0(g_uri_get_path(parsed), "/on_login_success") == 0 && query != NULL) {
        GHashTable* parameters = g_uri_parse_params(query, -1, "&", G_URI_PARAMS_NONE, NULL);
        if (parameters != NULL) {
            const gchar* value = g_hash_table_lookup(parameters, "code");
            if (value != NULL && *value != '\0') code = g_strdup(value);
            g_hash_table_unref(parameters);
        }
    }
    g_uri_unref(parsed);
    return code;
}

static gboolean On_Decide_Policy_(WebKitWebView* view, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type, gpointer user) {
    (void)view;
    (void)user;
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) return FALSE;
    WebKitNavigationAction* action = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    const gchar* uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));
    gchar* code = uri != NULL ? Code_From_(uri) : NULL;
    if (code == NULL) return FALSE; /* every other page loads as usual */
    fputs(code, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    g_free(code);
    webkit_policy_decision_ignore(decision); /* the success page itself is never loaded */
    s_exit_code = 0;
    gtk_main_quit();
    return TRUE;
}

static void On_Uri_(WebKitWebView* view, GParamSpec* spec, gpointer window);
static GtkWidget* Popup_(WebKitWebView* view, WebKitNavigationAction* action, gpointer user);

/* A page asked for a window of its own (Google's and the other "continue with" sign-ins open
 * one): a related view, so it shares this ephemeral session, with the same redirect watch. */
static void On_Popup_Ready_(WebKitWebView* view, gpointer window) {
    (void)view;
    gtk_widget_show_all(GTK_WIDGET(window));
}

static void On_Popup_Close_(WebKitWebView* view, gpointer window) {
    (void)view;
    gtk_widget_destroy(GTK_WIDGET(window));
}

static GtkWidget* Popup_(WebKitWebView* view, WebKitNavigationAction* action, gpointer user) {
    (void)action;
    (void)user;
    WebKitWebView* popup = WEBKIT_WEB_VIEW(webkit_web_view_new_with_related_view(view));
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), 520, 700);
    gtk_window_set_title(GTK_WINDOW(window), "Sign in to GOG");
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(popup));
    g_signal_connect(popup, "decide-policy", G_CALLBACK(On_Decide_Policy_), NULL);
    g_signal_connect(popup, "notify::uri", G_CALLBACK(On_Uri_), window);
    g_signal_connect(popup, "create", G_CALLBACK(Popup_), NULL);
    g_signal_connect(popup, "ready-to-show", G_CALLBACK(On_Popup_Ready_), window);
    g_signal_connect(popup, "close", G_CALLBACK(On_Popup_Close_), window);
    return GTK_WIDGET(popup);
}

/* The title shows which site the page is on, so the player can see it is GOG's. */
static void On_Uri_(WebKitWebView* view, GParamSpec* spec, gpointer window) {
    (void)spec;
    const gchar* uri = webkit_web_view_get_uri(view);
    GUri* parsed = uri != NULL ? g_uri_parse(uri, G_URI_FLAGS_NONE, NULL) : NULL;
    gchar* title = g_strdup_printf("Sign in to GOG - %s", parsed != NULL && g_uri_get_host(parsed) != NULL ? g_uri_get_host(parsed) : "");
    gtk_window_set_title(GTK_WINDOW(window), title);
    g_free(title);
    if (parsed != NULL) g_uri_unref(parsed);
}

int main(int argc, char** argv) {
    /* WebKitGTK's DMA-BUF renderer draws a blank page on NVIDIA's driver ("Failed to create GBM
     * buffer"; checked 2026-11-24): use the plain one unless the player chose otherwise. */
    g_setenv("WEBKIT_DISABLE_DMABUF_RENDERER", "1", FALSE);
    gtk_init(&argc, &argv);
    WebKitWebsiteDataManager* data = webkit_website_data_manager_new_ephemeral();
    WebKitWebContext* context = webkit_web_context_new_with_website_data_manager(data);
    WebKitWebView* view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(context));
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(window), 520, 760);
    gtk_window_set_title(GTK_WINDOW(window), "Sign in to GOG");
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(view));
    g_signal_connect(view, "decide-policy", G_CALLBACK(On_Decide_Policy_), NULL);
    g_signal_connect(view, "notify::uri", G_CALLBACK(On_Uri_), window);
    g_signal_connect(view, "create", G_CALLBACK(Popup_), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    const gboolean test_page = argc > 2 && strcmp(argv[1], "--page") == 0;
    if (test_page) { /* tests only: a page may open a window without a click */
        webkit_settings_set_javascript_can_open_windows_automatically(webkit_web_view_get_settings(view), TRUE);
    }
    webkit_web_view_load_uri(view, test_page ? argv[2] : GOG_AUTH_URL);
    gtk_widget_show_all(window);
    gtk_main();
    g_object_unref(context);
    g_object_unref(data);
    return s_exit_code;
}
