// Web UI пошуку (TCK-026, FR-031…FR-032). Логіка подання винесена в чисті функції, щоб її можна
// було тестувати без браузера. Текст із сервера вставляється лише через textContent.

// Порожній запит не надсилається; користувач бачить підказку (BDD-WEB-05).
export function validateQuery(raw) {
  const query = String(raw ?? '').trim();
  if (query.length === 0) {
    return { ok: false, hint: 'Введіть запит для пошуку.' };
  }
  return { ok: true, query };
}

export function formatScore(score) {
  return Number(score).toFixed(4);
}

// Подія keyword_search → модель результатів у порядку відповіді (BDD-WEB-02).
export function resultsFromEvent(event) {
  const items = (event?.data?.results ?? []).map((hit) => ({
    docId: String(hit.doc_id),
    score: formatScore(hit.score),
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

function render(model) {
  const status = document.getElementById('status');
  const list = document.getElementById('results');
  const hint = document.getElementById('hint');
  list.replaceChildren();
  hint.textContent = model.hint ?? '';
  status.className = model.error ? 'error' : '';
  status.textContent = model.status ?? '';
  for (const item of model.items ?? []) {
    const li = document.createElement('li');
    const title = document.createElement('div');
    title.className = 'doc-id';
    title.textContent = item.docId;
    const score = document.createElement('span');
    score.className = 'score';
    score.textContent = ` (score ${item.score})`;
    title.append(score);
    const snippet = document.createElement('p');
    snippet.className = 'snippet';
    snippet.textContent = item.snippet;
    li.append(title, snippet);
    list.append(li);
  }
}

function wsUrl() {
  const scheme = location.protocol === 'https:' ? 'wss:' : 'ws:';
  return `${scheme}//${location.host}/ws/search`;
}

// Поточне з'єднання. Новий пошук закриває попереднє; його події ігноруються (гонка двох запитів).
let currentSocket = null;

function runSearch(query) {
  if (currentSocket) {
    const previous = currentSocket;
    currentSocket = null;
    previous.close();
  }
  const socket = new WebSocket(wsUrl());
  currentSocket = socket;
  let received = false;
  let finished = false;
  const isCurrent = () => currentSocket === socket;
  const finish = (model) => {
    finished = true;
    if (isCurrent()) {
      currentSocket = null;
      if (model) {
        render(model);
      }
    }
    socket.close();
  };
  render({ status: 'Пошук…' });
  socket.addEventListener('open', () => socket.send(JSON.stringify({ query })));
  socket.addEventListener('message', (message) => {
    if (!isCurrent()) {
      return;
    }
    let event;
    try {
      event = JSON.parse(message.data);
    } catch {
      finish({ status: 'Помилка сервера.', error: true });
      return;
    }
    const model = classifyStage(event);
    if (model.type === 'expansion') {
      render({ status: 'Розширення запиту…' });
    } else if (model.type === 'results') {
      received = true;
      render({ status: model.empty ? 'Нічого не знайдено.' : '', items: model.items });
    } else if (model.type === 'error') {
      finish({ status: model.message, error: true });
    } else if (model.type === 'done') {
      if (received) {
        finish(null);
      } else {
        finish({ status: 'Відповідь неповна: результатів не отримано.', error: true });
      }
    }
  });
  socket.addEventListener('error', () => {
    if (!finished && isCurrent()) {
      finish({ status: unavailableMessage(), error: true });
    }
  });
  socket.addEventListener('close', () => {
    if (!finished && isCurrent()) {
      const message = received ? 'Відповідь неповна: з\'єднання перервано.' : unavailableMessage();
      finish({ status: message, error: true });
    }
  });
}

function init() {
  const form = document.getElementById('search-form');
  const input = document.getElementById('query');
  form.addEventListener('submit', (event) => {
    event.preventDefault();
    const checked = validateQuery(input.value);
    if (!checked.ok) {
      render({ hint: checked.hint });
      return;
    }
    render({ hint: '' });
    runSearch(checked.query);
  });
}

if (typeof document !== 'undefined') {
  init();
}
