  (function () {
    var cfg = window.__TPG && window.__TPG.config;
    if (!cfg) { return; }
    // Origin бэкенда (для абсолютных URL: /download живёт на балансировщике, а страница — на статике).
    var serverOrigin = '';
    try { serverOrigin = new URL(cfg.serverUrl, location.href).origin; } catch (e) { serverOrigin = ''; }
    var trustHost = document.getElementById('tpl-trust-editor');
    var cppHost = document.getElementById('tpl-cpp-editor');
    var status = document.getElementById('tpl-status');
    var healthEl = document.getElementById('tpl-health');
    var healthText = document.getElementById('tpl-health-text');
    var examplesSel = document.getElementById('tpl-examples');
    var log = document.getElementById('tpl-log');
    var downloadBtn = document.getElementById('tpl-download');
    var followCb = document.getElementById('tpl-follow-cb');
    var cppOverlay = document.getElementById('tpl-cpp-overlay');
    if (!trustHost || !cppHost) { return; }

    // Редакторы создаются в Monaco-колбэке; вынесены в область видимости glue,
    // чтобы к ним был доступ и из обработчиков комбобокса.
    var trustEditor = null, cppEditor = null;

    // Исходник последнего загруженного примера (для детекции изменений текста).
    var loadedSource = cfg.source;
    // Индекс текущего выбранного примера в комбобоксе (для отката при отмене).
    var curExIndex = -1;

    function appendLog(msg) {
      if (!log) { return; }
      log.textContent += (log.textContent ? '\n' : '') + msg;
      log.scrollTop = log.scrollHeight;
    }
    function clearLog() { if (log) { log.textContent = ''; } }

    // Показ/скрытие центрированного сообщения поверх правой панели (ошибки,
    // нет связи с сервером песочницы). По умолчанию оверлей скрыт (display:none).
    function setCppOverlay(html) {
      if (!cppOverlay) { return; }
      cppOverlay.innerHTML = html;
      cppOverlay.style.display = 'flex';
    }
    function clearCppOverlay() {
      if (!cppOverlay) { return; }
      cppOverlay.style.display = 'none';
      cppOverlay.innerHTML = '';
    }

    function setDownloadDisabled(on) {
      if (!downloadBtn) { return; }
      if (on) { downloadBtn.classList.add('tpl-btn-disabled'); downloadBtn.href = '#'; }
      else { downloadBtn.classList.remove('tpl-btn-disabled'); }
    }

    // Ленивое скачивание build-архива: POST /download — отдельный запрос, заново
    // обрабатывает текущий код и сразу возвращает tar.gz. НЕ зависит от /run.
    function downloadArchive() {
      if (!downloadBtn || !cfg.serverUrl) { return; }
      var body = (trustEditor && trustEditor.getValue) ? trustEditor.getValue() : (loadedSource || '');
      setStatus('building archive…');
      appendLog('building archive…');
      fetch(serverOrigin + '/download', {
        method: 'POST',
        headers: { 'Content-Type': 'text/plain; charset=utf-8' },
        body: body
      }).then(function (res) {
        if (!res.ok) {
          return res.json().catch(function () { return {}; }).then(function (d) {
            throw new Error((d && d.error) || ('archive build failed (' + res.status + ')'));
          });
        }
        var name = 'trust-build.tar.gz';
        var cd = res.headers.get('Content-Disposition');
        if (cd) { var m = /filename="?([^";]+)/.exec(cd); if (m && m[1]) { name = m[1]; } }
        return res.blob().then(function (blob) {
          var a = document.createElement('a');
          a.href = URL.createObjectURL(blob);
          a.download = name;
          document.body.appendChild(a);
          a.click();
          document.body.removeChild(a);
          URL.revokeObjectURL(a.href);
          setStatus('ok');
          appendLog('archive: ' + name);
        });
      }).catch(function (err) {
        var m = 'archive build failed: ' + err;
        setStatus(m);
        appendLog(m);
      });
    }
    if (downloadBtn) {
      downloadBtn.addEventListener('click', function (ev) { ev.preventDefault(); downloadArchive(); });
    }

    // Заполняем комбобокс примеров СРАЗУ (до загрузки Monaco) — он не может быть пустым.
    function populateExamples() {
      if (!examplesSel || !cfg.examples || cfg.examples.length === 0) { return; }
      var matched = -1;
      for (var i = 0; i < cfg.examples.length; i++) {
        var opt = document.createElement('option');
        opt.value = i;
        opt.textContent = cfg.examples[i].name;
        examplesSel.appendChild(opt);
        if (cfg.examples[i].source === cfg.source) { matched = i; }
      }
      if (matched >= 0) {
        curExIndex = matched;
        examplesSel.selectedIndex = matched;
      } else {
        // Текущий текст не совпадает ни с одним примером → отключённая опция «Custom».
        var custom = document.createElement('option');
        custom.value = -1;
        custom.textContent = 'Custom';
        custom.disabled = true;
        examplesSel.insertBefore(custom, examplesSel.firstChild);
        examplesSel.selectedIndex = 0;
        curExIndex = -1;
      }
      examplesSel.onchange = function () {
        var idx = parseInt(examplesSel.value, 10);
        if (!cfg.examples[idx]) { return; }
        var ex = cfg.examples[idx];
        if (trustEditor && trustEditor.getValue() !== loadedSource) {
          if (!confirm('Текущий текст будет заменён на пример "' + ex.name + '". Продолжить?')) {
            if (curExIndex >= 0) { examplesSel.selectedIndex = curExIndex; }
            else { examplesSel.selectedIndex = 0; }
            return;
          }
        }
        if (trustEditor) { trustEditor.setValue(ex.source); }
        loadedSource = ex.source;
        curExIndex = idx;
        setStatus('loaded example ' + ex.name);
      };
    }
    populateExamples();

    function setStatus(msg) { if (status) { status.textContent = msg; } }

    function setHealth(state, txt) {
      if (!healthEl) { return; }
      healthEl.className = 'tpl-health ' + state;
      if (healthText) { healthText.textContent = txt; }
    }

    // Публичный пинг балансировщика: онлайн ли он и сколько воркеров активно.
    // Постоянный статус готовности, отличает «нет связи с балансировщиком» от «нет воркеров».
    // Терпим к старым балансировщикам: если в ответе нет workers_connected — всё равно «онлайн»,
    // просто без счётчика (поле появилось в новой версии /health).
    function updateHealth() {
      if (!healthEl) { return; }
      if (!cfg.serverUrl) { setHealth('down', 'нет связи с балансировщиком'); return; }
      fetch(serverOrigin + '/health', { method: 'GET' }).then(function (res) {
        if (!res.ok) {
          console.warn('[trust-playground] /health HTTP ' + res.status + ' at ' + serverOrigin);
          setHealth('down', 'нет связи с балансировщиком (HTTP ' + res.status + ')'); return;
        }
        return res.json().catch(function () {
          console.warn('[trust-playground] /health вернул не JSON (вероятно, статическая страница вместо балансировщика): ' + serverOrigin);
          return null;
        });
      }).then(function (d) {
        if (!d || d.status !== 'ok') {
          console.warn('[trust-playground] /health ответ без status=ok:', d);
          setHealth('down', 'нет связи с балансировщиком'); return;
        }
        var n = (typeof d.workers_connected === 'number') ? d.workers_connected : null;
        if (n === null) { setHealth('ok', 'балансировщик онлайн'); }
        else if (n > 0) { setHealth('ok', 'балансировщик онлайн · воркеров: ' + n); }
        else { setHealth('degraded', 'балансировщик онлайн · нет воркеров'); }
      }).catch(function (e) {
        console.warn('[trust-playground] /health fetch failed at ' + serverOrigin + ':', e);
        setHealth('down', 'нет связи с балансировщиком');
      });
    }

    function loadScript(src, onload) {
      var s = document.createElement('script');
      s.src = src;
      s.onload = onload;
      s.onerror = function () { setStatus('Failed to load Monaco from ' + src); };
      document.head.appendChild(s);
    }

    function mkDeco(lines, cls) {
      var arr = [];
      for (var i = 0; i < lines.length; i++) {
        arr.push({ range: new monaco.Range(lines[i], 1, lines[i], 1),
                   options: { isWholeLine: true, className: cls } });
      }
      return arr;
    }

    function initEditors() {
      if (typeof require === 'undefined') { setStatus('Monaco loader unavailable'); return; }
      require.config({ paths: { vs: cfg.monacoUrl } });
      require(['vs/editor/editor.main'], function () {
        try {
          /* trust Monarch tokenizer */
          monaco.languages.register({ id: 'trust' });
          monaco.languages.setMonarchTokensProvider('trust', __MONARCH__);

          trustEditor = monaco.editor.create(trustHost, {
            value: cfg.source, language: 'trust', theme: 'vs',
            readOnly: false, automaticLayout: true, scrollBeyondLastLine: false,
            minimap: { enabled: true }
          });
          cppEditor = monaco.editor.create(cppHost, {
            // Трансляция НЕ хранится в шаблоне страницы — правый редактор
            // стартует пустым и заполняется только из ответа балансировщика.
            value: '', language: 'cpp', theme: 'vs',
            readOnly: true, automaticLayout: true, scrollBeyondLastLine: false,
            minimap: { enabled: true }
          });

          var t2c = {}, c2t = {};
          var trustDec = [], cppDec = [];
          var trustGutterDec = [], cppGutterDec = [];

          function followEnabled() { return !followCb || followCb.checked; }

          // Gutter-маркеры: пометить в колонке номеров ВСЕ строки, у которых есть маппинг
          // (постоянные декор-метки; не путать с breadcrumb текущей строки).
          function mkGutter(lines) {
            var arr = [];
            for (var i = 0; i < lines.length; i++) {
              arr.push({ range: new monaco.Range(lines[i], 1, lines[i], 1),
                         options: { isWholeLine: true, linesDecorationsClassName: 'tpl-gutter' } });
            }
            return arr;
          }
          function buildGutterMarks() {
            var tl = [], cl = [];
            for (var k in t2c) { if (t2c[k] && t2c[k].length) { tl.push(Number(k)); } }
            for (var k in c2t) { if (c2t[k] && c2t[k].length) { cl.push(Number(k)); } }
            trustGutterDec = trustEditor.deltaDecorations(trustGutterDec, mkGutter(tl));
            cppGutterDec = cppEditor.deltaDecorations(cppGutterDec, mkGutter(cl));
          }
          buildGutterMarks();

          // Троттлинг reveal: не «дёргать» прокрутку при быстрых перемещениях курсора.
          var revealTimers = { trust: 0, cpp: 0 };
          function revealThrottled(editor, line, which) {
            if (!followEnabled()) { return; }
            clearTimeout(revealTimers[which]);
            revealTimers[which] = setTimeout(function () { editor.revealLine(line); }, 80);
          }

          // Навигация + breadcrumb: подсветить соответствующие строки, прокрутить (если follow),
          // и показать маппинг ТЕКУЩЕЙ строки в статус-баре («→ cpp: N»).
          trustEditor.onDidChangeCursorPosition(function (e) {
            var l = e.position.lineNumber;
            var lines = (t2c[l] || []);
            cppDec = cppEditor.deltaDecorations(cppDec, mkDeco(lines, 'tpl-linked'));
            if (lines.length) {
              revealThrottled(cppEditor, lines[0], 'cpp');
              setStatus('→ cpp: ' + lines.join(', '));
            }
          });
          cppEditor.onDidChangeCursorPosition(function (e) {
            var l = e.position.lineNumber;
            var lines = (c2t[l] || []);
            trustDec = trustEditor.deltaDecorations(trustDec, mkDeco(lines, 'tpl-linked'));
            if (lines.length) {
              revealThrottled(trustEditor, lines[0], 'trust');
              setStatus('→ trust: ' + lines.join(', '));
            }
          });

          // Двойной клик: перейти к первой соответствующей строке в противоположной панели.
          trustEditor.onMouseDown(function (e) {
            var dl = t2c[e.target.position.lineNumber];
            if (e.event && e.event.detail >= 2 && dl && dl.length) {
              cppEditor.setPosition({ lineNumber: dl[0], column: 1 });
              cppEditor.revealLineInCenter(dl[0]);
            }
          });
          cppEditor.onMouseDown(function (e) {
            var dl = c2t[e.target.position.lineNumber];
            if (e.event && e.event.detail >= 2 && dl && dl.length) {
              trustEditor.setPosition({ lineNumber: dl[0], column: 1 });
              trustEditor.revealLineInCenter(dl[0]);
            }
          });

          function resetCppPane(html) {
            // Очищаем правую панель и показываем по центру сообщение об ошибке/нет связи.
            if (cppEditor && cppEditor.setValue) { cppEditor.setValue(''); }
            t2c = {}; c2t = {};
            cppDec = cppEditor.deltaDecorations(cppDec, []);
            cppGutterDec = cppEditor.deltaDecorations(cppGutterDec, []);
            trustGutterDec = trustEditor.deltaDecorations(trustGutterDec, []);
            setCppOverlay(html);
            setDownloadDisabled(true);
          }

          function recompile() {
            var body = trustEditor.getValue();
            setStatus('transpiling…');
            clearLog();
            appendLog('transpiling…');
            fetch(cfg.serverUrl, {
              method: 'POST',
              headers: { 'Content-Type': 'text/plain; charset=utf-8' },
              body: body
            }).then(function (res) {
              // Читаем тело как текст и пытаемся распарсить JSON: балансировщик
              // может вернуть HTML/прокси-ошибку (не JSON) — в этом случае трактуем
              // как отсутствие связи/ошибку и не роняем цепочку.
              return res.text().then(function (txt) {
                var data = null;
                try { data = JSON.parse(txt); } catch (e) { data = null; }
                return { ok: res.ok, status: res.status, data: data };
              });
            }).then(function (rr) {
              var data = rr.data;
              if (data && data.unavailable) {
                var umsg = (data.error || 'Нет свободных воркеров');
                if (data.instructionsUrl) {
                  umsg = umsg + ' — <a href="' + data.instructionsUrl + '" target="_blank" rel="noopener">запустите свой узел</a>';
                }
                setStatus(umsg);
                appendLog(umsg);
                resetCppPane(umsg);
                return;
              }
              if (!rr.ok) {
                // Балансировщик/прокси вернул HTTP-ошибку без валидного JSON-контракта.
                var herr = (data && data.error) ? data.error : ('Нет связи с балансировщиком (HTTP ' + rr.status + ')');
                setStatus(herr);
                appendLog(herr);
                resetCppPane(herr);
                return;
              }
              if (!data || !data.ok) {
                var err = (data && data.error) ? data.error : 'Ошибка транспиляции';
                setStatus(err);
                appendLog(err);
                if (data && data.log) { appendLog(data.log); }
                resetCppPane(err);
                return;
              }
              cppEditor.setValue(data.cpp);
              t2c = data.trustToCpp || {}; c2t = data.cppToTrust || {};
              cppDec = cppEditor.deltaDecorations(cppDec, []);
              clearCppOverlay();
              setDownloadDisabled(false);
              setStatus('ok');
              appendLog('ok');
              if (data.log) { appendLog(data.log); }
            }).catch(function (err) {
              // Сетевой сбой (нет связи с балансировщиком).
              var m = 'Нет связи с балансировщиком';
              setStatus(m);
              appendLog('request failed: ' + err);
              resetCppPane(m);
            });
          }

          setStatus('ready');
          if (cfg.serverUrl) {
            var timer = null;
            trustEditor.onDidChangeModelContent(function () {
              clearTimeout(timer);
              timer = setTimeout(recompile, 400);
            });
            // Первичная транспиляция на загрузке (заполняет лог). Кнопка «⬇ Скачать»
            // активна только после первого успешного ответа балансировщика.
            recompile();
          }
        } catch (err) {
          setStatus('init error: ' + err);
        }
      });
    }

    updateHealth();
    setInterval(updateHealth, 5000);

    loadScript(cfg.monacoUrl + '/loader.js', initEditors);
  })();
