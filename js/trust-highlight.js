/* trust-highlight.js
 * Статическая подсветка блоков кода Trust (`<code class="language-trust">`)
 * тем же Monarch-токенайзером, что и в песочнице (src/lsp/trust.monarch.js).
 * Мини-раннер исполняет правила токенизации построчно (со стеком состояний
 * для многострочных комментариев и строк) и оборачивает токены в span-ы
 * с классами t-* (цвета задаются в _styles_project.scss).
 */
(function () {
  "use strict";

  // Monarch-токенайзер Trust (правила совпадают с песочницей).
  var RULES = {
    root: [
      [/\/\*/, "comment.block", "@comment"],
      [/#.*$/, "comment.line"],
      [/@\w[\w_]*/, "keyword.macro"],
      [/@[{}[\]]<>:|/, "keyword.control"],
      [/\$\$|\$\*|\$\.\.\.|\$\^/, "variable.language"],
      [/\$\d+/, "variable.language"],
      [/\$[A-Za-z_]\w*/, "variable"],
      [/:[A-Za-z_]\w*/, "type"],
      [/%[A-Za-z_]\w*/, "keyword.function"],
      [/R"[\s\S]*?"/, "string.raw"],
      [/R'[\s\S]*?'/, "string.raw"],
      [/`[^`]*`/, "string"],
      [/"/, "string.double", "@str_d"],
      [/'/, "string.single", "@str_s"],
      [/-?\d[\d_]*\.\d+([eE][-+]?\d+)?|-?\d[\d_]*([eE][-+]?\d+)?/, "number"],
      [/[a-zA-Z_]\w*/, "identifier"],
      [/[{}()[\]]/, "@brackets"],
      [/[=:;,.+\-*\/%&|^~!<>]+/, "operator"],
      [/\s+/, "white"]
    ],
    comment: [
      [/[^/*]+/, "comment.block"],
      [/\/\*/, "comment.block", "@push"],
      [/\*\//, "comment.block", "@pop"],
      [/[/*]/, "comment.block"]
    ],
    str_d: [
      [/[^"\\]+/, "string.double"],
      [/\\./, "string.escape"],
      [/"/, "string.double", "@pop"]
    ],
    str_s: [
      [/[^'\\]+/, "string.single"],
      [/\\./, "string.escape"],
      [/'/, "string.single", "@pop"]
    ]
  };

  // Прогоняет одну строку через токенизатор, обновляя стек состояний.
  function tokenize(stack, line) {
    var out = [];
    var i = 0;
    var n = line.length;
    while (i < n) {
      var state = stack[stack.length - 1] || "root";
      var rules = RULES[state] || RULES.root;
      var done = false;
      for (var r = 0; r < rules.length; r++) {
        var rule = rules[r];
        var m = rule[0].exec(line.substring(i));
        if (m && m.index === 0 && m[0].length > 0) {
          var type = rule[1];
          var len = m[0].length;
          if (type && type.charAt(0) !== "@") out.push([i, i + len, type]);
          var next = rule[2];
          // Monarch-семантика: все переходы кроме @pop — push (открывают
          // под-состояние: @str_d/@str_s/@comment для строк/комментариев,
          // @push — вложенный комментарий). @pop — выход из состояния.
          if (next === "@pop") {
            if (stack.length > 1) stack.pop();
          } else if (next && next.charAt(0) === "@") {
            stack.push(next.slice(1));
          }
          i += len;
          done = true;
          break;
        }
      }
      if (!done) i++;
    }
    return out;
  }

  var ESC = { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" };
  function esc(s) {
    return s.replace(/[&<>"]/g, function (c) { return ESC[c]; });
  }

  function cssClass(type) {
    if (type.indexOf("comment") === 0) return "t-comment";
    if (type.indexOf("keyword.macro") === 0) return "t-macro";
    if (type.indexOf("keyword.control") === 0) return "t-control";
    if (type.indexOf("keyword.function") === 0) return "t-func";
    if (type.indexOf("string") === 0) return "t-string";
    if (type.indexOf("number") === 0) return "t-number";
    if (type.indexOf("type") === 0) return "t-type";
    if (type.indexOf("variable.language") === 0) return "t-varlang";
    if (type.indexOf("variable") === 0) return "t-var";
    if (type.indexOf("operator") === 0) return "t-op";
    if (type.indexOf("brackets") === 0) return "t-bracket";
    return "t-ident";
  }

  function highlight(code) {
    var stack = ["root"];
    var lines = code.split("\n");
    var html = "";
    for (var li = 0; li < lines.length; li++) {
      var line = lines[li];
      var tokens = tokenize(stack, line);
      var pos = 0;
      for (var t = 0; t < tokens.length; t++) {
        var tk = tokens[t];
        html += esc(line.substring(pos, tk[0]));
        html += '<span class="' + cssClass(tk[2]) + '">' + esc(line.substring(tk[0], tk[1])) + "</span>";
        pos = tk[1];
      }
      html += esc(line.substring(pos));
      if (li < lines.length - 1) html += "\n";
    }
    return html;
  }

  function run() {
    // Обрабатываем как `<code class="language-trust">` (стандарт Hugo для ```trust),
    // так и `<pre class="language-trust">` (класс может быть на pre).
    var nodes = document.querySelectorAll("code.language-trust, pre.language-trust");
    for (var i = 0; i < nodes.length; i++) {
      var node = nodes[i];
      var el = (node.tagName === "CODE") ? node : node.querySelector("code");
      if (!el || el.getAttribute("data-trust-highlighted")) continue;
      var code = el.textContent;
      if (!code) continue;
      el.innerHTML = highlight(code);
      el.setAttribute("data-trust-highlighted", "1");
    }
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", run);
  } else {
    run();
  }
})();
