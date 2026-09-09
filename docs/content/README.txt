Document project in Docsy format (symlink to site/content)
1. Base language - russian (content/ru is the ONLY original source of documentation)
2. content/en is a synchronous translation of content/ru (never an original source)
3. Handmade translate *.md and *.html files to target (en) language
4. Convert /ru/ links to /en/ language
5. English pages must NOT link to Russian-language originals (ru.wikipedia -> en.wikipedia;
   habr.com/ru, anekdot.ru, etc. -> drop the link, keep the text; drop front matter `link:` with ru URL)
6. Copy CNAME to target dir (docs/gh-pages)
7. Copy folder `gh-pages` to `gh-pages` branch into `git@github.com:afteri-ru/trust-lang.git`
