#import <Cocoa/Cocoa.h>

int main() {
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];

        NSRect frame = NSMakeRect(0, 0, 480, 320);
        NSWindow* window = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:NSWindowStyleMaskTitled |
                                NSWindowStyleMaskClosable |
                                NSWindowStyleMaskMiniaturizable |
                                NSWindowStyleMaskResizable
                        backing:NSBackingStoreBuffered
                          defer:NO];
        [window setTitle:@"STOW Archive"];
        [window center];

        NSTextField* label = [NSTextField labelWithString:@"Hello from native macOS UI"];
        [label setFrame:NSMakeRect(24, 250, 300, 24)];
        [[window contentView] addSubview:label];

        [window makeKeyAndOrderFront:nil];
        [application activateIgnoringOtherApps:YES];
        [application run];
    }

    return 0;
}
