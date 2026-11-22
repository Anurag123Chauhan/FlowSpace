export const positioning = 'FlowSpace — Independent virtual desktops for each monitor on Windows.';
export const repository = 'https://github.com/Anurag123Chauhan/FlowSpace';
export const release = `${repository}/releases/latest`;
export const product = `${repository}/blob/main/docs/PRODUCT.md`;

export const pages = [
    {
        path: '',
        title: positioning,
        description: 'Free, open-source virtual desktop workspaces for each monitor on Windows 10 and 11. Switch one screen without changing the others.',
        heading: positioning,
        content: link => `
            <p class="lede">Keep a reference on one screen while you switch between projects on the other. FlowSpace gives each display its own set of workspaces without changing Windows Task View.</p>
            <p><a class="button" href="${link('download/')}">Download for Windows</a> <a href="${repository}">View source on GitHub</a></p>
            <figure>
                <picture>
                    <source media="(prefers-reduced-motion: reduce)" srcset="${link('assets/social-preview.png')}">
                    <img src="${link('assets/monitor-switching.svg')}" width="960" height="400" alt="Monitor 1 switches from Development to Design while Monitor 2 stays on Personal.">
                </picture>
                <figcaption>Illustration: switch the workspace on one display while the other stays put.</figcaption>
            </figure>
            <h2>What can I do with FlowSpace?</h2>
            <ul>
                <li>Keep a call, notes, or documentation visible on one monitor while switching projects on another.</li>
                <li>Give each workspace a name, color, and optional wallpaper.</li>
                <li>Use a keyboard shortcut or the system tray to switch without closing your apps.</li>
            </ul>
            <p>Free and open source under the MIT license. Runs on Windows 10 (1903+) and Windows 11, x64. See the <a href="${link('guides/')}">setup guides</a> or <a href="${link('compare/windows-virtual-desktops/')}">compare with Windows virtual desktops</a>.</p>`,
    },
    {
        path: 'features/',
        title: 'Per-monitor workspace features | FlowSpace',
        description: 'Explore monitor-specific workspaces, hotkeys, wallpapers, app rules, and window recovery in FlowSpace for Windows.',
        heading: 'FlowSpace features',
        content: link => `
            <h2>Independent workspaces</h2>
            <p>Each connected display has its own workspace list and active selection. Switching on one display leaves the other displays' workspace selections alone.</p>
            <h2>Keyboard and tray switching</h2>
            <p>With the default settings, point at a monitor and press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>←</kbd> or <kbd>→</kbd>. You can also select a workspace from the tray menu.</p>
            <h2>Wallpapers and app rules</h2>
            <p>Name and color workspaces, assign optional wallpaper images, and route matching new windows with App Rules in Settings.</p>
            <h2>Window recovery</h2>
            <p>FlowSpace records windows before hiding them so it can reveal still-open windows after an unexpected exit. It does not save application data.</p>
            <p><a href="${link('docs/')}">Read the user documentation</a>.</p>`,
    },
    {
        path: 'download/',
        title: 'Download FlowSpace for Windows 10 and 11 (x64)',
        description: 'Get the free FlowSpace x64 installer or portable ZIP from the official GitHub release. Supports Windows 10 (1903+) and Windows 11.',
        heading: 'Download FlowSpace for Windows',
        content: link => `
            <p class="lede">Free, MIT-licensed software for Windows 10 (1903+) and Windows 11, x64. No admin rights required.</p>
            <p><a class="button" href="${release}">Open the latest GitHub release</a></p>
            <h2>Choose your download</h2>
            <ul>
                <li><strong>Installer:</strong> under Assets, choose the file ending in <code>-windows-x64-setup.exe</code> and run it.</li>
                <li><strong>Portable:</strong> download the file ending in <code>-windows-x64.zip</code>, extract it, and run <code>flowspace.exe</code>.</li>
            </ul>
            <p>Look for the FlowSpace icon in the system tray, including the overflow menu. Next, <a href="${link('docs/')}">try your first workspace switch</a>.</p>`,
    },
    {
        path: 'docs/',
        title: 'FlowSpace documentation: setup, shortcuts, and recovery',
        description: 'Learn how to use FlowSpace on Windows: create a workspace, switch monitors with shortcuts, assign an app, and reveal hidden windows.',
        heading: 'FlowSpace documentation',
        content: link => `
            <h2>Create and switch workspaces</h2>
            <ol>
                <li><a href="${link('download/')}">Download and start FlowSpace</a>, then open Settings from its tray icon.</li>
                <li>Open <strong>Workspaces</strong>. Under the display you want to change, choose <strong>Add workspace</strong>.</li>
                <li>Point at that display and use <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>→</kbd> to cycle forward. You can also use the tray menu to select a workspace by name.</li>
                <li>To move an existing window to the next workspace, put the pointer over it and press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>Shift</kbd> + <kbd>→</kbd>.</li>
            </ol>
            <h2>Assign new windows with App Rules</h2>
            <p>Focus a window on the target workspace, press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>O</kbd>, then go to <strong>App Rules → Add rule</strong>. The new rule captures the previously focused app and its monitor's active workspace.</p>
            <h2>Reveal windows or troubleshoot shortcuts</h2>
            <p>Use <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>F12</kbd> or <strong>Reveal All Windows</strong> in the tray. If a shortcut doesn't work, open Settings from the tray and check <strong>Shortcuts</strong> for conflicts.</p>
            <h2>Reference</h2>
            <ul>
                <li><a href="${product}">Product facts and limitations</a></li>
                <li><a href="${repository}/blob/main/docs/engineering/CONFIGURATION.md">Configuration schema</a></li>
                <li><a href="${repository}/blob/main/UNINSTALL.md">Uninstall instructions</a></li>
            </ul>`,
    },
    {
        path: 'guides/',
        title: 'Guides for using virtual desktops on multiple monitors | FlowSpace',
        description: 'Practical guides to setting up independent virtual desktops on Windows 11, switching just one of two monitors, and organizing a multi-monitor desk.',
        heading: 'Multi-monitor workspace guides',
        content: link => `
            <ul>
                <li><a href="${link('guides/independent-virtual-desktops-windows-11/')}">Set up independent virtual desktops on Windows 11</a></li>
                <li><a href="${link('guides/different-desktops-on-two-monitors/')}">Keep different desktops on two monitors</a></li>
                <li><a href="${link('guides/multi-monitor-productivity-setup/')}">Build a multi-monitor productivity setup</a></li>
            </ul>`,
    },
    {
        path: 'guides/independent-virtual-desktops-windows-11/',
        title: 'How to set up independent virtual desktops on Windows 11 | FlowSpace',
        description: 'Use FlowSpace to switch workspaces separately on each Windows 11 monitor, without changing the system-wide Task View desktop.',
        heading: 'Set up independent virtual desktops on Windows 11',
        content: link => `
            <p>Windows 11 Task View changes the active native desktop across your displays. To change the windows on just one monitor, use FlowSpace workspaces instead.</p>
            <ol>
                <li>In Windows display settings, select <strong>Extend these displays</strong> rather than duplicating your screens.</li>
                <li><a href="${link('download/')}">Install and start FlowSpace</a>. Open Settings from the tray and check that each display has its own workspace list.</li>
                <li>Add a workspace on the display you want to switch. Move the pointer there and press <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>→</kbd>.</li>
                <li>Move the pointer to another display and try the same shortcut. Only the display under the pointer switches with the default configuration.</li>
            </ol>
            <p><strong>Tip:</strong> Test from one Windows Task View desktop first. FlowSpace does not manage windows Windows has hidden on other native desktops.</p>
            <p><a href="${link('compare/windows-virtual-desktops/')}">Compare FlowSpace with Task View</a>.</p>`,
    },
    {
        path: 'guides/different-desktops-on-two-monitors/',
        title: 'How to keep different desktops on two monitors in Windows | FlowSpace',
        description: 'Keep a document or call visible on your second display while cycling through workspaces on your primary monitor.',
        heading: 'Keep different desktops on two monitors',
        content: link => `
            <p>Try a simple setup: keep a reference document on your right monitor while you alternate between research and writing on your left.</p>
            <ol>
                <li>In FlowSpace Settings, create <strong>Research</strong> and <strong>Writing</strong> on the left monitor. Leave the right monitor on its current workspace.</li>
                <li>Open your reference document on the right monitor. Select Research on the left and open the windows you want to use there.</li>
                <li>Use the tray menu to select Writing on the left monitor, then open your writing app there. The reference document should stay visible on the right.</li>
                <li>Point at the left monitor and use the default previous/next shortcuts to switch back and forth.</li>
            </ol>
            <p>For a clean first test, keep each window fully inside one display. FlowSpace assigns a window to one display; it does not split a spanning window.</p>
            <p><a href="${link('docs/')}">See the shortcuts and recovery controls</a>.</p>`,
    },
    {
        path: 'guides/multi-monitor-productivity-setup/',
        title: 'A practical multi-monitor productivity setup for Windows | FlowSpace',
        description: 'Organize a Windows multi-monitor desk into a stable reference screen and purpose-built workspaces for tasks that change throughout the day.',
        heading: 'Build a multi-monitor productivity setup',
        content: link => `
            <h2>Give each display a job</h2>
            <p>Keep the windows you need all day on one display: your reference notes, calendar, or call. Use the other display for work that changes in blocks, such as writing and reviewing.</p>
            <h2>Start with two workspaces</h2>
            <p>On the active display, try <strong>Focus</strong> for the task at hand and <strong>Review</strong> for feedback or tests. Use different names, accent colors, or wallpapers so you can recognize the current workspace at a glance.</p>
            <h2>Add rules only where they help</h2>
            <p>If a dedicated app always belongs in one workspace, create an App Rule. Leave shared tools unrouted if you regularly open them for different tasks.</p>
            <h2>Keep an escape hatch</h2>
            <p>Remember <kbd>Ctrl</kbd> + <kbd>Alt</kbd> + <kbd>F12</kbd> or <strong>Reveal All Windows</strong> in the tray if you lose track of a window while experimenting.</p>
            <p><a href="${link('docs/')}">Read the setup and App Rules instructions</a>.</p>`,
    },
    {
        path: 'compare/windows-virtual-desktops/',
        title: 'FlowSpace vs. Windows virtual desktops (Task View)',
        description: 'Compare Windows Task View with FlowSpace workspaces: whole-desktop switching versus independent switching on each display.',
        heading: 'FlowSpace vs. Windows virtual desktops',
        content: link => `
            <p>Choose Task View when you want to change the working context across your whole display setup. Choose FlowSpace when you want to change one monitor and leave the others alone.</p>
            <table>
                <caption>Switching behavior on a multi-monitor Windows PC</caption>
                <thead><tr><th scope="col">Question</th><th scope="col">Windows Task View</th><th scope="col">FlowSpace</th></tr></thead>
                <tbody>
                    <tr><th scope="row">What switches?</th><td>The active native desktop across displays.</td><td>The workspace of the selected display.</td></tr>
                    <tr><th scope="row">How are windows separated?</th><td>Windows manages native desktop membership.</td><td>FlowSpace hides and shows eligible windows on the current native desktop.</td></tr>
                    <tr><th scope="row">Does it require another app?</th><td>No; it is built into Windows.</td><td>Yes; FlowSpace runs in the system tray.</td></tr>
                </tbody>
            </table>
            <p>FlowSpace isn't a Task View extension. Windows stays in control of its native desktops and of windows cloaked on them.</p>
            <p>For native desktop controls and settings, see <a href="https://support.microsoft.com/en-us/windows/experience/configure-multiple-desktops-in-windows">Microsoft's multiple desktops documentation</a>.</p>
            <p><a href="${link('guides/different-desktops-on-two-monitors/')}">Try an example with two monitors</a>.</p>`,
    },
    {
        path: 'faq/',
        title: 'FlowSpace FAQ: Windows support, licensing, and recovery',
        description: 'Answers about FlowSpace pricing, Windows 10 and 11 support, native virtual desktops, and what window recovery can and cannot do.',
        heading: 'FlowSpace FAQ',
        content: link => `
            <h2>Is FlowSpace free and open source?</h2>
            <p>Yes. It is free and its source is available under the MIT license on <a href="${repository}">GitHub</a>.</p>
            <h2>Which Windows versions are supported?</h2>
            <p>Windows 10 version 1903 or newer and Windows 11, x64. An installer and a portable ZIP are available on the <a href="${link('download/')}">download page</a>.</p>
            <h2>Does FlowSpace add native desktops to Task View?</h2>
            <p>No. It manages the visibility of eligible windows on the current Windows desktop. It does not change how Task View works.</p>
            <h2>Can FlowSpace restore my session after a reboot?</h2>
            <p>No. Its recovery journal can reveal windows that are still open after FlowSpace exits unexpectedly. It cannot reopen apps or recover unsaved work.</p>
            <h2>What kinds of windows may not switch?</h2>
            <p>Shell windows, some floating tools and dialogs, and windows Windows has cloaked (including those on other Task View desktops) are excluded.</p>`,
    },
];
