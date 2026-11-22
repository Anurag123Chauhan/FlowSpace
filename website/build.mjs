import { copyFile, mkdir, rm, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { pages, positioning, product, release, repository } from './pages.mjs';

const output = new URL('../build/site/', import.meta.url);
const base = new URL(process.env.SITE_URL || 'https://anurag123chauhan.github.io/FlowSpace/');

if (base.protocol !== 'https:' || base.username || base.password || base.search || base.hash) {
    throw new Error('SITE_URL must be an HTTPS site URL without credentials, a query, or a fragment.');
}
if (!base.pathname.endsWith('/')) base.pathname += '/';

const absolute = path => new URL(path, base).href;
const link = path => new URL(path, base).pathname;
const escape = value => value.replace(/[&<>"']/g, char => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
})[char]);

const navigation = [
    ['features/', 'Features'],
    ['download/', 'Download'],
    ['docs/', 'Docs'],
    ['guides/', 'Guides'],
    ['compare/windows-virtual-desktops/', 'Compare'],
    ['faq/', 'FAQ'],
];

const software = {
    '@context': 'https://schema.org',
    '@type': 'SoftwareApplication',
    '@id': `${absolute('')}#software`,
    name: 'FlowSpace',
    description: pages[0].description,
    url: absolute(''),
    operatingSystem: 'Windows 10 (1903 or later) and Windows 11 (x64)',
    applicationCategory: 'UtilitiesApplication',
    downloadUrl: release,
    isAccessibleForFree: true,
    offers: {
        '@type': 'Offer',
        price: 0,
        url: absolute('download/'),
    },
};

function render(page) {
    const canonical = absolute(page.path);
    const preview = absolute('assets/social-preview.png');
    const nav = navigation.map(([path, label]) => {
        const current = page.path === path ? ' aria-current="page"' : '';
        return `<a href="${link(path)}"${current}>${label}</a>`;
    }).join('\n            ');
    const homeMetadata = page.path === '' ? `
    <meta name="google-site-verification" content="x-t4oXNYIvm8jVw4BYGa1xSLPUcGrPoM2KHdtiiFoL4" />
    <script type="application/ld+json">${JSON.stringify(software, null, 4).replaceAll('<', '\\u003c')}</script>` : '';
    return `<!doctype html>
<html lang="en">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>${escape(page.title)}</title>
    <meta name="description" content="${escape(page.description)}">
    <link rel="canonical" href="${escape(canonical)}">
    <meta property="og:type" content="website">
    <meta property="og:site_name" content="FlowSpace">
    <meta property="og:title" content="${escape(page.title)}">
    <meta property="og:description" content="${escape(page.description)}">
    <meta property="og:url" content="${escape(canonical)}">
    <meta property="og:image" content="${escape(preview)}">
    <meta property="og:image:alt" content="FlowSpace artwork showing two independent monitor workspaces">
    <meta name="twitter:card" content="summary_large_image">
    <link rel="stylesheet" href="${link('assets/site.css')}">${homeMetadata}
</head>
<body>
    <a class="skip" href="#main">Skip to content</a>
    <header>
        <a class="brand" href="${link('')}">FlowSpace</a>
        <nav aria-label="Main">
            ${nav}
        </nav>
    </header>
    <main id="main">
        <h1>${escape(page.heading)}</h1>
        ${page.content(link)}
    </main>
    <footer><p>${escape(positioning)}</p>
        <p><a href="${repository}">Source on GitHub</a> · <a href="${product}">Product facts</a> · <a href="${repository}/blob/main/LICENSE">MIT license</a></p>
    </footer>
</body>
</html>
`;
}

for (const page of pages) {
    const directory = new URL(page.path, output);
    await mkdir(directory, { recursive: true });
    await writeFile(new URL('index.html', directory), render(page));
}

const assets = new URL('assets/', output);
await mkdir(assets, { recursive: true });
await copyFile(new URL('site.css', import.meta.url), new URL('site.css', assets));
for (const name of ['monitor-switching.svg', 'social-preview.png']) {
    await copyFile(new URL(`../docs/assets/${name}`, import.meta.url), new URL(name, assets));
}

const sitemap = pages.map(page => `    <url><loc>${escape(absolute(page.path))}</loc></url>`).join('\n');
await writeFile(new URL('sitemap.xml', output), `<?xml version="1.0" encoding="UTF-8"?>
<urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">
${sitemap}
</urlset>
`);
await writeFile(new URL('llms.txt', output), `# FlowSpace

> ${positioning} Free and open source (MIT). Windows 10 (1903+) and Windows 11, x64.

## Official resources

- [Home](${absolute('')}): Product overview and independent switching demonstration.
- [Product facts](${product}): Supported platforms, features, and limitations.
- [Download](${absolute('download/')}): Installer and portable ZIP.
- [Documentation](${absolute('docs/')}): Setup, shortcuts, and recovery.
- [Guides](${absolute('guides/')}): Multi-monitor workflows.
- [Comparison](${absolute('compare/windows-virtual-desktops/')}): How FlowSpace differs from Windows Task View.
- [FAQ](${absolute('faq/')}): Support, licensing, and compatibility.
`);

const robots = new URL('robots.txt', output);
if (base.pathname === '/') {
    await writeFile(robots, `User-agent: *\nAllow: /\nSitemap: ${absolute('sitemap.xml')}\n`);
} else {
    await rm(robots, { force: true });
}

console.log(`Built ${pages.length} pages for ${base.href} in ${fileURLToPath(output)}`);
