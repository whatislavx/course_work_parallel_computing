// Web UI Sherlok (TCK-026, FR-031…FR-034). Логіка подання винесена в чисті функції, щоб її можна
// було тестувати без браузера (`node --test web/test`). Текст із сервера вставляється лише через textContent.

// Порожній запит не надсилається; користувач бачить підказку (BDD-WEB-05).
export function validateQuery(raw) {
  const query = String(raw ?? '').trim();
  if (query.length === 0) {
    return { ok: false, hint: 'Введіть запит для пошуку.' };
  }
  return { ok: true, query };
}

export function formatScore(score) {
  return Number(score).toFixed(2);
}

// Підпис бала в картці результату: пояснює, що число — релевантність документа запиту.
export function scoreLabel(score) {
  return `Релевантність ${score}`;
}

// Подія keyword_search → модель результатів у порядку відповіді (BDD-WEB-02).
export function resultsFromEvent(event) {
  const hits = event?.data?.results ?? [];
  const best = Math.max(...hits.map((hit) => Number(hit.score)), 0);
  const items = hits.map((hit) => ({
    docId: String(hit.doc_id),
    title: String(hit.title || hit.doc_id),
    score: formatScore(hit.score),
    share: best > 0 ? Number(hit.score) / best : 0,
    snippet: String(hit.snippet ?? ''),
  }));
  return { items, empty: items.length === 0 };
}

// Подія error → текст data.message (BDD-WEB-06).
export function errorMessageFromEvent(event) {
  const message = event?.data?.message;
  return typeof message === 'string' && message.length > 0 ? message : 'Помилка сервера.';
}

export function unavailableMessage() {
  return 'Пошуковий сервер недоступний. Перевірте, що сервіс запущено.';
}

// Класифікує подію етапу для відображення.
export function classifyStage(event) {
  switch (event?.stage) {
    case 'query_expansion':
      return { type: 'expansion', terms: event.data?.expanded_terms ?? [] };
    case 'keyword_search':
      return { type: 'results', ...resultsFromEvent(event) };
    case 'error':
      return { type: 'error', message: errorMessageFromEvent(event) };
    case 'done':
      return { type: 'done' };
    default:
      return { type: 'unknown' };
  }
}


// Порядок етапів для смуги прогресу. Назви — для користувача, без внутрішніх деталей (FR-033).
export const STAGE_ORDER = ['query_expansion', 'keyword_search', 'done'];
export const STAGE_LABELS = {
  query_expansion: 'Уточнення запиту',
  keyword_search: 'Пошук документів',
  done: 'Готово',
};

// Стан кожного етапу за останнім отриманим: пройдені — done, поточний — active, решта — pending.
export function progressStates(lastStage) {
  // Передача між етапами: уточнення завершено, пошук ще не почався.
  if (lastStage === 'handoff') {
    return STAGE_ORDER.map((stage) => ({
      stage, label: STAGE_LABELS[stage], state: stage === 'query_expansion' ? 'done' : 'pending',
    }));
  }
  const index = STAGE_ORDER.indexOf(lastStage);
  return STAGE_ORDER.map((stage, position) => {
    let state = 'pending';
    if (lastStage === 'done' || (index >= 0 && position < index)) {
      state = 'done';
    } else if (index >= 0 && position === index) {
      state = 'active';
    }
    return { stage, label: STAGE_LABELS[stage], state };
  });
}

// Підпис етапу з реальними значеннями: час від запиту (виміряний у браузері) або з сервера,
// розширення від Ollama, кількість знайдених документів. Нічого не вигадуємо і не затримуємо.
export function stageDetail(stage, payload) {
  switch (stage) {
    case 'query_expansion': {
      const terms = Array.isArray(payload.expanded_terms) ? payload.expanded_terms : [];
      return terms.length > 0
        ? `за ${payload.elapsed_ms} мс · розширено: ${terms.join(', ')}`
        : `за ${payload.elapsed_ms} мс · без додаткових слів`;
    }
    case 'keyword_search':
      return `за ${payload.time_ms} мс · документів: ${payload.count}`;
    case 'done':
      return `загалом ${payload.elapsed_ms} мс`;
    default:
      return '';
  }
}

// Українська множина: 1 документ, 2-4 документи, 5+ документів (11-14 — завжди «документів»).
export function pluralUk(count, forms) {
  const n = Math.abs(Number(count));
  const last = n % 10;
  const lastTwo = n % 100;
  if (lastTwo >= 11 && lastTwo <= 14) {
    return forms[2];
  }
  if (last === 1) {
    return forms[0];
  }
  return last >= 2 && last <= 4 ? forms[1] : forms[2];
}

export function foundMessage(count) {
  return `Знайдено ${count} ${pluralUk(count, ['документ', 'документи', 'документів'])}`;
}

// Розбиває текст на частини для підсвічування слів запиту; без innerHTML, лише текстові вузли.
export function highlightParts(text, query) {
  const terms = String(query ?? '').toLowerCase().split(/[^\p{L}\p{N}]+/u).filter(Boolean);
  const source = String(text ?? '');
  if (terms.length === 0) {
    return [{ text: source, match: false }];
  }
  const escaped = terms.map((term) => term.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'));
  const pattern = new RegExp(`(?<![\\p{L}\\p{N}])(${escaped.join('|')})(?![\\p{L}\\p{N}])`, 'giu');
  return source.split(pattern).filter((part) => part !== '')
    .map((part) => ({ text: part, match: terms.includes(part.toLowerCase()) }));
}

// Тривалість анімації кроку смуги етапів: результати показуються після неї, а не раніше.
export const STAGE_PAUSE_MS = 250;

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

// Останній запит: для підсвічування і повторної спроби.
let lastQuery = '';

// Елементи необов'язкові: на головній сторінці немає списку результатів.
function render(model) {
  const status = document.getElementById('status');
  const list = document.getElementById('results');
  const hint = document.getElementById('hint');
  if (hint) {
    hint.textContent = model.hint ?? '';
  }
  if (status) {
    status.className = model.error ? 'error' : '';
    status.textContent = model.status ?? '';
    if (model.retry && lastQuery) {
      const retry = document.createElement('button');
      retry.type = 'button';
      retry.className = 'retry';
      retry.textContent = 'Повторити';
      retry.addEventListener('click', () => runSearch(lastQuery));
      status.append(retry);
    }
  }
  if (!list) {
    return;
  }
  list.replaceChildren();
  if (model.empty) {
    const card = document.createElement('li');
    card.className = 'empty-state';
    const title = document.createElement('strong');
    title.textContent = 'Нічого не знайдено';
    card.append(title, 'Спробуйте інше слово або перевірте написання.');
    list.append(card);
  }
  for (const [index, item] of (model.items ?? []).entries()) {
    const li = document.createElement('li');
    li.style.setProperty('--i', String(index));
    const title = document.createElement('div');
    title.className = 'doc-title';
    title.textContent = item.title;
    const score = document.createElement('span');
    score.className = 'score';
    score.style.setProperty('--share', item.share.toFixed(3));
    const bar = document.createElement('span');
    bar.className = 'score-bar';
    bar.setAttribute('aria-hidden', 'true');
    const value = document.createElement('span');
    value.textContent = scoreLabel(item.score);
    score.append(bar, value);
    score.title = 'Релевантність: чим більше, тим краще документ відповідає запиту (сума tf·idf за словами запиту)';
    title.append(score);
    const snippet = document.createElement('p');
    snippet.className = 'snippet';
    for (const part of highlightParts(item.snippet, lastQuery)) {
      if (part.match) {
        const mark = document.createElement('mark');
        mark.textContent = part.text;
        snippet.append(mark);
      } else {
        snippet.append(part.text);
      }
    }
    li.append(title, snippet);
    list.append(li);
  }
}

// Смуга етапів: пройдені з реальним часом, поточний — «триває…», решта очікують.
// Елемент необов'язковий: на головній сторінці його немає.
function renderProgress(lastStage, details = {}, visible = true) {
  const list = document.getElementById('progress');
  if (!list) {
    return;
  }
  list.hidden = !visible;
  if (!visible) {
    return;
  }
  // Крок «done» не показується окремо: завершення видно за станом кроків, а час — у заголовку.
  const steps = progressStates(lastStage).filter((item) => item.stage !== 'done');
  // Елементи кроків переживають оновлення, щоб CSS-переходи між станами були плавними.
  if (list.children.length !== steps.length) {
    list.replaceChildren(...steps.map(createStep));
  }
  steps.forEach((item, position) => updateStep(list.children[position], item, position, details));
}

function createStep() {
  const li = document.createElement('li');
  const node = document.createElement('span');
  node.className = 'step-node';
  const text = document.createElement('span');
  text.className = 'step-text';
  const label = document.createElement('span');
  label.className = 'stage-label';
  const detail = document.createElement('span');
  detail.className = 'stage-detail';
  text.append(label, detail);
  li.append(node, text);
  return li;
}

function updateStep(li, item, position, details) {
  li.dataset.state = item.state;
  if (item.state === 'active') {
    li.setAttribute('aria-current', 'step');
  } else {
    li.removeAttribute('aria-current');
  }
  li.querySelector('.step-node').textContent = item.state === 'done' ? '✓' : String(position + 1);
  li.querySelector('.stage-label').textContent = item.label;
  li.querySelector('.stage-detail').textContent =
    item.state === 'active' ? 'триває…' : (details[item.stage] ?? '');
}

// Заглушки-скелети під час очікування відповіді.
function renderSkeleton() {
  const list = document.getElementById('results');
  if (!list) {
    return;
  }
  list.replaceChildren(
    ...Array.from({ length: 3 }, (_, index) => {
      const li = document.createElement('li');
      li.className = 'skeleton';
      li.setAttribute('aria-hidden', 'true');
      li.style.setProperty('--i', String(index));
      return li;
    }),
  );
}

function wsUrl() {
  const scheme = location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${scheme}//${location.host}/ws/search`;
}

// Поточне з'єднання. Новий пошук закриває попереднє; його події ігноруються (гонка двох запитів).
let currentSocket = null;

function runSearch(query) {
  lastQuery = query;
  if (currentSocket) {
    const previous = currentSocket;
    currentSocket = null;
    previous.close();
  }
  const socket = new WebSocket(wsUrl());
  currentSocket = socket;
  let received = false;
  let statusText = '';
  let finished = false;
  const isCurrent = () => currentSocket === socket;
  const finish = (model) => {
    finished = true;
    if (isCurrent()) {
      currentSocket = null;
      if (model) {
        render(model);
      }
      if (model?.error) {
        renderProgress(null, {}, false);
      }
    }
    socket.close();
  };
  const startedAt = performance.now();
  const elapsed = () => Math.round(performance.now() - startedAt);
  const details = {};
  render({ status: 'Пошук…' });
  renderProgress(null);
  renderSkeleton();
  socket.addEventListener('open', () => socket.send(JSON.stringify({ query })));
  const handleMessage = async (message) => {
    if (!isCurrent()) {
      return;
    }
    let event;
    try {
      event = JSON.parse(message.data);
    } catch {
      finish({ status: 'Помилка сервера.', error: true, retry: true });
      return;
    }
    const model = classifyStage(event);
    if (model.type === 'expansion') {
      details.query_expansion = stageDetail('query_expansion', {
        expanded_terms: model.terms,
        elapsed_ms: elapsed(),
      });
      // Анімація: уточнення завершується, лінія доходить до «Пошуку документів», він стає активним.
      await sleep(Math.max(0, STAGE_PAUSE_MS - elapsed()));
      for (const stage of ['handoff', 'keyword_search']) {
        if (finished || !isCurrent()) {
          return;
        }
        renderProgress(stage, details);
        await sleep(STAGE_PAUSE_MS);
      }
    } else if (model.type === 'results') {
      details.keyword_search = stageDetail('keyword_search', {
        time_ms: event.data?.time_ms ?? 0,
        count: model.items.length,
      });
      received = true;
      statusText = model.empty ? '' : foundMessage(model.items.length);
      render({
        status: statusText,
        items: model.items,
        empty: model.empty,
      });
    } else if (model.type === 'error') {
      finish({ status: model.message, error: true, retry: true });
    } else if (model.type === 'done') {
      if (received) {
        renderProgress('done', details);
        const statusEl = document.getElementById('status');
        if (statusEl && statusText) {
          statusEl.textContent = `${statusText} · ${stageDetail('done', { elapsed_ms: elapsed() })}`;
        }
        finish(null);
      } else {
        finish({ status: 'Відповідь неповна: результатів не отримано.', error: true, retry: true });
      }
    }
  };
  let queue = Promise.resolve();
  socket.addEventListener('message', (message) => {
    queue = queue.then(() => handleMessage(message));
  });
  socket.addEventListener('error', () => {
    if (!finished && isCurrent()) {
      finish({ status: unavailableMessage(), error: true, retry: true });
    }
  });
  // Закриття обробляється після всіх повідомлень у черзі, щоб не обірвати анімацію перед результатами.
  socket.addEventListener('close', () => {
    queue = queue.then(() => {
      if (!finished && isCurrent()) {
        const message = received ? 'Відповідь неповна: з\'єднання перервано.' : unavailableMessage();
        finish({ status: message, error: true, retry: true });
      }
    });
  });
}

function init() {
  const form = document.getElementById('search-form');
  const input = document.getElementById('query');
  if (!form || !input) {
    return;
  }
  const onResultsPage = document.body?.dataset?.page === 'results';
  if (onResultsPage) {
    const initial = (new URLSearchParams(location.search).get('q') ?? '').trim();
    input.value = initial;
    if (initial.length > 0) {
      runSearch(initial);
    }
  }
  document.addEventListener('keydown', (event) => {
    const typing = /^(INPUT|TEXTAREA)$/.test(document.activeElement?.tagName ?? '');
    if (event.key === '/' && !typing && !event.ctrlKey && !event.metaKey) {
      event.preventDefault();
      input.focus();
    }
  });
  form.addEventListener('submit', (event) => {
    event.preventDefault();
    const checked = validateQuery(input.value);
    if (!checked.ok) {
      render({ hint: checked.hint });
      return;
    }
    render({ hint: '' });
    const target = `/results?q=${encodeURIComponent(checked.query)}`;
    if (onResultsPage) {
      history.replaceState(null, '', target);
      runSearch(checked.query);
    } else {
      location.assign(target);
    }
  });
}

if (typeof document !== 'undefined') {
  // Сторінка завжди відкривається зверху: відновлена позиція стрибає, коли результати підміняють скелети.
  if ('scrollRestoration' in history) {
    history.scrollRestoration = 'manual';
  }
  window.scrollTo(0, 0);
  init();
}
