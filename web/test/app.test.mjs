// Тести логіки Web UI (TCK-026): запуск через `node --test web/test`.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  validateQuery, formatScore, resultsFromEvent, errorMessageFromEvent, classifyStage,
} from '../static/app.js';

test('empty or blank query is not sent and gets a hint (BDD-WEB-05)', () => {
  assert.equal(validateQuery('').ok, false);
  assert.equal(validateQuery('   ').ok, false);
  assert.equal(validateQuery(undefined).ok, false);
  assert.equal(validateQuery('  parallel ').query, 'parallel');
});

test('score is shown with four decimals', () => {
  assert.equal(formatScore(0.5), '0.5000');
});

test('keyword_search results keep the server order and carry doc_id, score, snippet (BDD-WEB-02)', () => {
  const model = resultsFromEvent({
    stage: 'keyword_search',
    data: {
      results: [
        { doc_id: 'b.txt', score: 2, snippet: 'alpha beta' },
        { doc_id: 'a.txt', score: 1, snippet: 'alpha' },
      ],
      time_ms: 1,
    },
  });
  assert.deepEqual(model.items.map((i) => i.docId), ['b.txt', 'a.txt']);
  assert.equal(model.items[0].score, '2.0000');
  assert.equal(model.empty, false);
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
