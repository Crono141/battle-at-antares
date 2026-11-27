/* antares-gog-signin (macOS): GOG's own sign-in page in a window (WP5.17b, D29 case 3).
 *
 * The Mac twin of main_gtk.c, on WKWebView. Same contract with the launcher: when GOG sends the
 * page to https://embed.gog.com/on_login_success?...&code=..., the one-time code goes to standard
 * output and the program exits 0; closing the window exits 1. The session uses a non-persistent
 * data store (nothing on disk), so nothing outlives the window, and no script is injected into the
 * page: the redirect is seen through WebKit's navigation policy. Built with ARC. */
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

static NSString* const GOG_AUTH_URL = @"https://auth.gog.com/auth?client_id=46899977096215655&redirect_uri=https%3A%2F%2Fembed.gog.com%2Fon_login_success%3Forigin%3Dclient"
                                       @"&response_type=code&layout=client2";

/* The code from the success address, or nil when the address isn't GOG's success page. */
static NSString* Code_From_(NSURL* url) {
    if (url == nil) return nil;
    if ([url.scheme caseInsensitiveCompare:@"https"] != NSOrderedSame) return nil;
    if (url.host == nil || [url.host caseInsensitiveCompare:@"embed.gog.com"] != NSOrderedSame) return nil;
    if (![url.path isEqualToString:@"/on_login_success"]) return nil;
    NSURLComponents* parts = [NSURLComponents componentsWithURL:url resolvingAgainstBaseURL:NO];
    for (NSURLQueryItem* item in parts.queryItems) {
        if ([item.name isEqualToString:@"code"] && item.value.length > 0) return item.value;
    }
    return nil;
}

@interface c_signin : NSObject <WKNavigationDelegate, WKUIDelegate, NSWindowDelegate>
@property(strong) NSWindow* window;
@property(strong) NSMutableArray<NSWindow*>* popups;
@end

@implementation c_signin

- (NSWindow*)windowWithView:(WKWebView*)view size:(NSSize)size {
    NSWindow* window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, size.width, size.height)
                                                   styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    window.title = @"Sign in to GOG";
    window.releasedWhenClosed = NO;
    window.contentView = view;
    view.navigationDelegate = self;
    view.UIDelegate = self;
    [window center];
    return window;
}

- (void)start:(NSURL*)url {
    WKWebViewConfiguration* configuration = [[WKWebViewConfiguration alloc] init];
    configuration.websiteDataStore = [WKWebsiteDataStore nonPersistentDataStore];
    WKWebView* view = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:configuration];
    self.popups = [NSMutableArray array];
    self.window = [self windowWithView:view size:NSMakeSize(520, 760)];
    self.window.delegate = self;
    [view loadRequest:[NSURLRequest requestWithURL:url]];
    [self.window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

- (void)webView:(WKWebView*)view decidePolicyForNavigationAction:(WKNavigationAction*)action decisionHandler:(void (^)(WKNavigationActionPolicy))decide {
    NSString* code = Code_From_(action.request.URL);
    if (code == nil) {
        decide(WKNavigationActionPolicyAllow); /* every other page loads as usual */
        return;
    }
    decide(WKNavigationActionPolicyCancel); /* the success page itself is never loaded */
    fputs(code.UTF8String, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    exit(0);
}

/* The title shows which site the page is on, so the player can see it is GOG's. */
- (void)webView:(WKWebView*)view didCommitNavigation:(WKNavigation*)navigation {
    view.window.title = [NSString stringWithFormat:@"Sign in to GOG - %@", view.URL.host ?: @""];
}

/* A page asked for a window of its own (Google's and the other "continue with" sign-ins open
 * one): WebKit's configuration for it keeps the same session and the opener. */
- (WKWebView*)webView:(WKWebView*)view createWebViewWithConfiguration:(WKWebViewConfiguration*)configuration forNavigationAction:(WKNavigationAction*)action windowFeatures:(WKWindowFeatures*)features {
    WKWebView* popup = [[WKWebView alloc] initWithFrame:NSZeroRect configuration:configuration];
    NSWindow* window = [self windowWithView:popup size:NSMakeSize(520, 700)];
    [self.popups addObject:window];
    [window makeKeyAndOrderFront:nil];
    return popup;
}

- (void)webViewDidClose:(WKWebView*)view {
    NSWindow* window = view.window;
    if (window == self.window) return;
    [window close];
    [self.popups removeObject:window];
}

- (void)windowWillClose:(NSNotification*)notification {
    exit(1); /* closed before signing in */
}
@end

int main(int argc, const char** argv) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        const BOOL test_page = argc > 2 && strcmp(argv[1], "--page") == 0; /* tests only */
        NSURL* url = [NSURL URLWithString:test_page ? [NSString stringWithUTF8String:argv[2]] : GOG_AUTH_URL];
        c_signin* signin = [[c_signin alloc] init];
        [signin start:url];
        [NSApp run];
    }
    return 1;
}
