// Тести логіки Web UI (TCK-026): запуск через `node --test web/test`.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  validateQuery,
  formatScore,
  resultsFromEvent,
  errorMessageFromEvent,
  classifyStage,
  progressStates,
  pluralUk,
  foundMessage,
  highlightParts,
  scoreLabel,
  stageDetail,
} from '../static/app.js';

test('empty or blank query is not sent and gets a hint (BDD-WEB-05)', () => {
  assert.equal(validateQuery('').ok, false);
  assert.equal(validateQuery('   ').ok, false);
  assert.equal(validateQuery(undefined).ok, false);
  assert.equal(validateQuery('  parallel ').query, 'parallel');
});

test('score label names the value as relevance', () => {
  assert.equal(scoreLabel('2.7716'), 'релевантність 2.7716');
});

test('score is shown with four decimals', () => {
  assert.equal(formatScore(0.5), '0.5000');
});

test('keyword_search results keep the server order and carry doc_id, title, score, snippet (BDD-WEB-02)', () => {
  const model = resultsFromEvent({
    stage: 'keyword_search',
    data: {
      results: [
        { doc_id: 'b.txt', score: 2, title: 'Beta notes', snippet: 'alpha beta' },
        { doc_id: 'a.txt', score: 1, title: 'Alpha', snippet: 'alpha' },
      ],
      time_ms: 1,
    },
  });
  assert.deepEqual(model.items.map((i) => i.docId), ['b.txt', 'a.txt']);
  assert.deepEqual(model.items.map((i) => i.title), ['Beta notes', 'Alpha']);
  assert.equal(model.items[0].score, '2.0000');
  assert.equal(model.empty, false);
});

test('title falls back to doc_id when the server sends none or an empty one', () => {
  const model = resultsFromEvent({
    stage: 'keyword_search',
    data: {
      results: [
        { doc_id: 'c.txt', score: 1, snippet: 'alpha' },
        { doc_id: 'd.txt', score: 1, title: '', snippet: 'alpha' },
      ],
      time_ms: 1,
    },
  });
  assert.deepEqual(model.items.map((i) => i.title), ['c.txt', 'd.txt']);
});

test('empty results are reported as empty', () => {
  assert.equal(resultsFromEvent({ stage: 'keyword_search', data: { results: [] } }).empty, true);
});

test('error event shows data.message (BDD-WEB-06)', () => {
  const event = { stage: 'error', data: { code: 'index_not_ready', message: 'index is not built yet' } };
  assert.equal(errorMessageFromEvent(event), 'index is not built yet');
  assert.equal(classifyStage(event).type, 'error');
});

test('query_expansion exposes expanded terms; unknown stages are ignored', () => {
  assert.deepEqual(
    classifyStage({ stage: 'query_expansion', data: { original: 'x', expanded_terms: [] } }).terms,
    [],
  );
  assert.equal(classifyStage({ stage: 'whatever' }).type, 'unknown');
  assert.equal(classifyStage({ stage: 'done' }).type, 'done');
});

test('done without data is accepted as completion (BDD-WEB-04)', () => {
  assert.deepEqual(classifyStage({ stage: 'done' }), { type: 'done' });
  assert.deepEqual(classifyStage({ stage: 'done', data: {} }), { type: 'done' });
});

test('query_expansion is classified as an expansion stage, not as results', () => {
  assert.equal(classifyStage({ stage: 'query_expansion', data: {} }).type, 'expansion');
});

test('progress marks passed stages done and the current stage active (FR-033)', () => {
  assert.deepEqual(progressStates('query_expansion').map((i) => i.state), ['active', 'pending', 'pending']);
  assert.deepEqual(progressStates('keyword_search').map((i) => i.state), ['done', 'active', 'pending']);
  assert.deepEqual(progressStates('done').map((i) => i.state), ['done', 'done', 'done']);
  assert.deepEqual(progressStates(null).map((i) => i.state), ['pending', 'pending', 'pending']);
});

test('Ukrainian plural forms for the result counter', () => {
  assert.equal(foundMessage(1), 'Знайдено 1 документ');
  assert.equal(foundMessage(3), 'Знайдено 3 документи');
  assert.equal(foundMessage(5), 'Знайдено 5 документів');
  assert.equal(foundMessage(11), 'Знайдено 11 документів');
  assert.equal(foundMessage(21), 'Знайдено 21 документ');
  assert.equal(pluralUk(12, ['а', 'б', 'в']), 'в');
});

test('highlightParts marks query terms case-insensitively and keeps the text intact', () => {
  const parts = highlightParts('Garden of gardens', 'garden');
  assert.equal(parts.map((p) => p.text).join(''), 'Garden of gardens');
  assert.deepEqual(parts.filter((p) => p.match).map((p) => p.text), ['Garden']);
  assert.deepEqual(highlightParts('plain text', ''), [{ text: 'plain text', match: false }]);
  assert.equal(highlightParts('a.b (c)', '.*(').map((p) => p.text).join(''), 'a.b (c)');
});

test('expansion detail shows measured time and the expanded terms', () => {
  assert.equal(
    stageDetail('query_expansion', { expanded_terms: ['car', 'auto'], elapsed_ms: 120 }),
    'за 120 мс · розширено: car, auto',
  );
});

test('expansion detail says so when no extra terms were added (fallback)', () => {
  assert.equal(
    stageDetail('query_expansion', { expanded_terms: [], elapsed_ms: 80 }),
    'за 80 мс · без додаткових слів',
  );
});

test('keyword detail shows server time and document count', () => {
  assert.equal(stageDetail('keyword_search', { time_ms: 12, count: 3 }), 'за 12 мс на сервері · документів: 3');
});

test('done detail shows total measured time', () => {
  assert.equal(stageDetail('done', { elapsed_ms: 340 }), 'загалом 340 мс');
});
