/*
 * Docsy docs: hide/show of the whole left (section) sidebar with a mouse.
 *
 * Desktop only (>= xl), only on pages of the `docs` section (marker class
 * `.td-docs-layout` on `.row` in layouts/docs/baseof.html). Progressive
 * enhancement: without JS (or below xl, or when printing) the layout stays as
 * stock Docsy and nothing changes.
 *
 * Behaviour:
 *  - adds `.td-sidebar--ready` to the layout row, which reveals a small
 *    "tab" button at the left edge of the content (styled in
 *    assets/scss/_styles_project.scss);
 *  - a click toggles the `.td-sidebar--collapsed` class (hides the whole
 *    `.td-sidebar` column and lets the <main> fill the freed space);
 *  - the collapsed state is persisted in localStorage
 *    (key `trust-docs-sidebar-hidden`), so the choice survives navigation;
 *  - the button always sits in empty gutter space (sidebar's right padding /
 *    <main>'s large left padding), never over menu/content text.
 */
(function () {
  'use strict';

  var row = document.querySelector('.td-docs-layout');
  if (!row || !window.localStorage) {
    return; // keep the stock Docsy layout
  }
  var aside = row.querySelector(':scope > .td-sidebar');
  var mqXL = window.matchMedia('(min-width: 1200px)');
  var LS_KEY = 'trust-docs-sidebar-hidden';
  var btn = null;
  var active = false;

  function readPref() {
    try {
      return localStorage.getItem(LS_KEY) === '1';
    } catch (e) {
      return false;
    }
  }

  function writePref(hidden) {
    try {
      if (hidden) {
        localStorage.setItem(LS_KEY, '1');
      } else {
        localStorage.removeItem(LS_KEY);
      }
    } catch (e) {
      /* storage may be unavailable (private mode, quotas) - keep in-memory state */
    }
  }

  function setCollapsed(collapsed) {
    row.classList.toggle('td-sidebar--collapsed', collapsed);
    writePref(collapsed);
    updateBtn(collapsed);
    placeBtn();
  }

  function placeBtn() {
    if (!btn || !active) {
      return;
    }
    var collapsed = row.classList.contains('td-sidebar--collapsed');
    var left;
    if (collapsed) {
      // Sidebar is hidden: pin the tab to the very left edge of the content.
      left = (aside ? aside.offsetLeft : 0) + 6;
    } else {
      // Expanded: put the tab right on the sidebar/content boundary (both sides
      // there are empty paddings).
      left = aside ? aside.offsetLeft + aside.offsetWidth : 0;
    }
    btn.style.left = left + 'px';
  }

  function updateBtn(collapsed) {
    if (!btn) {
      return;
    }
    var icon = btn.querySelector('i');
    if (icon) {
      icon.className = 'fa-solid ' + (collapsed ? 'fa-chevron-right' : 'fa-chevron-left');
    }
    btn.title = collapsed ? 'Показать меню разделов' : 'Скрыть меню разделов';
    btn.setAttribute('aria-expanded', collapsed ? 'false' : 'true');
  }

  function makeBtn() {
    btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'td-sidebar__collapse';
    btn.innerHTML = '<i class="fa-solid fa-chevron-left" aria-hidden="true"></i>';
    btn.setAttribute('aria-label', 'Меню разделов');
    btn.addEventListener('click', function () {
      setCollapsed(!row.classList.contains('td-sidebar--collapsed'));
    });
    row.appendChild(btn);
  }

  function enable() {
    if (active) {
      return;
    }
    active = true;
    makeBtn();
    row.classList.add('td-sidebar--ready');
    setCollapsed(readPref()); // restore the persisted choice (only meaningful at xl)
  }

  function disable() {
    if (!active) {
      return;
    }
    active = false;
    row.classList.remove('td-sidebar--ready', 'td-sidebar--collapsed');
    if (btn) {
      btn.remove();
      btn = null;
    }
  }

  function refresh() {
    if (mqXL.matches) {
      enable();
    } else {
      disable(); // below xl - always fall back to the stock layout
    }
  }

  if (mqXL.addEventListener) {
    mqXL.addEventListener('change', refresh);
  } else if (mqXL.addListener) {
    mqXL.addListener(refresh); // legacy Safari
  }
  window.addEventListener('resize', placeBtn);
  refresh();
})();
