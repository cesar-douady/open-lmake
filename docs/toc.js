// Populate the sidebar
//
// This is a script, and not included directly in the page, to control the total size of the book.
// The TOC contains an entry for each page, so if each page includes a copy of the TOC,
// the total size of the page becomes O(n**2).
class MDBookSidebarScrollbox extends HTMLElement {
    constructor() {
        super();
    }
    connectedCallback() {
        this.innerHTML = '<ol class="chapter"><li class="chapter-item expanded affix "><a href="overview.html">Overview</a></li><li class="chapter-item expanded affix "><a href="intro.html">Introduction</a></li><li class="chapter-item expanded affix "><a href="install.html">Installation</a></li><li class="chapter-item expanded "><a href="lmake_module.html"><strong aria-hidden="true">1.</strong> The lmake module</a></li><li><ol class="section"><li class="chapter-item expanded "><a href="lmake_sources_module.html"><strong aria-hidden="true">1.1.</strong> lmake.sources</a></li><li class="chapter-item expanded "><a href="lmake_rules_module.html"><strong aria-hidden="true">1.2.</strong> lmake.rules</a></li><li class="chapter-item expanded "><a href="lmake_import_machinery_module.html"><strong aria-hidden="true">1.3.</strong> lmake.import_machinery</a></li></ol></li><li class="chapter-item expanded "><a href="writing_lmakefile.html"><strong aria-hidden="true">2.</strong> Writing Lmakefile.py</a></li><li><ol class="section"><li class="chapter-item expanded "><a href="config.html"><strong aria-hidden="true">2.1.</strong> Config fields</a></li><li class="chapter-item expanded "><a href="rules.html"><strong aria-hidden="true">2.2.</strong> Rules attributes</a></li></ol></li><li class="chapter-item expanded "><a href="execution.html"><strong aria-hidden="true">3.</strong> Execution</a></li><li><ol class="section"><li class="chapter-item expanded "><a href="job_execution.html"><strong aria-hidden="true">3.1.</strong> Job execution</a></li><li class="chapter-item expanded "><a href="data_model.html"><strong aria-hidden="true">3.2.</strong> Data model</a></li><li class="chapter-item expanded "><a href="rule_selection.html"><strong aria-hidden="true">3.3.</strong> Rule selection</a></li><li class="chapter-item expanded "><a href="backends.html"><strong aria-hidden="true">3.4.</strong> Backends</a></li><li class="chapter-item expanded "><a href="namespaces.html"><strong aria-hidden="true">3.5.</strong> Namespaces</a></li><li class="chapter-item expanded "><a href="autodep.html"><strong aria-hidden="true">3.6.</strong> Autodep</a></li><li class="chapter-item expanded "><a href="critical_deps.html"><strong aria-hidden="true">3.7.</strong> Critical deps</a></li><li class="chapter-item expanded "><a href="eta.html"><strong aria-hidden="true">3.8.</strong> ETA</a></li><li class="chapter-item expanded "><a href="video_mode.html"><strong aria-hidden="true">3.9.</strong> Video mode</a></li></ol></li><li class="chapter-item expanded "><a href="cache.html"><strong aria-hidden="true">4.</strong> Cache</a></li><li class="chapter-item expanded "><a href="codec.html"><strong aria-hidden="true">5.</strong> Codec</a></li><li class="chapter-item expanded "><a href="meta_data.html"><strong aria-hidden="true">6.</strong> The LMAKE directory</a></li><li class="chapter-item expanded "><a href="commands.html"><strong aria-hidden="true">7.</strong> Commands</a></li><li class="chapter-item expanded "><a href="experimental.html"><strong aria-hidden="true">8.</strong> Experimental features</a></li><li><ol class="section"><li class="chapter-item expanded "><a href="experimental_subrepos.html"><strong aria-hidden="true">8.1.</strong> Subrepos</a></li></ol></li><li class="chapter-item expanded "><a href="glossary.html">Glossary</a></li><li class="chapter-item expanded affix "><a href="faq.html">FAQ</a></li></ol>';
        // Set the current, active page, and reveal it if it's hidden
        let current_page = document.location.href.toString().split("#")[0];
        if (current_page.endsWith("/")) {
            current_page += "index.html";
        }
        var links = Array.prototype.slice.call(this.querySelectorAll("a"));
        var l = links.length;
        for (var i = 0; i < l; ++i) {
            var link = links[i];
            var href = link.getAttribute("href");
            if (href && !href.startsWith("#") && !/^(?:[a-z+]+:)?\/\//.test(href)) {
                link.href = path_to_root + href;
            }
            // The "index" page is supposed to alias the first chapter in the book.
            if (link.href === current_page || (i === 0 && path_to_root === "" && current_page.endsWith("/index.html"))) {
                link.classList.add("active");
                var parent = link.parentElement;
                if (parent && parent.classList.contains("chapter-item")) {
                    parent.classList.add("expanded");
                }
                while (parent) {
                    if (parent.tagName === "LI" && parent.previousElementSibling) {
                        if (parent.previousElementSibling.classList.contains("chapter-item")) {
                            parent.previousElementSibling.classList.add("expanded");
                        }
                    }
                    parent = parent.parentElement;
                }
            }
        }
        // Track and set sidebar scroll position
        this.addEventListener('click', function(e) {
            if (e.target.tagName === 'A') {
                sessionStorage.setItem('sidebar-scroll', this.scrollTop);
            }
        }, { passive: true });
        var sidebarScrollTop = sessionStorage.getItem('sidebar-scroll');
        sessionStorage.removeItem('sidebar-scroll');
        if (sidebarScrollTop) {
            // preserve sidebar scroll position when navigating via links within sidebar
            this.scrollTop = sidebarScrollTop;
        } else {
            // scroll sidebar to current active section when navigating via "next/previous chapter" buttons
            var activeSection = document.querySelector('#sidebar .active');
            if (activeSection) {
                activeSection.scrollIntoView({ block: 'center' });
            }
        }
        // Toggle buttons
        var sidebarAnchorToggles = document.querySelectorAll('#sidebar a.toggle');
        function toggleSection(ev) {
            ev.currentTarget.parentElement.classList.toggle('expanded');
        }
        Array.from(sidebarAnchorToggles).forEach(function (el) {
            el.addEventListener('click', toggleSection);
        });
    }
}
window.customElements.define("mdbook-sidebar-scrollbox", MDBookSidebarScrollbox);
