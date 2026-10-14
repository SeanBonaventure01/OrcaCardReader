// Loads the API contract fixtures shared with the C++ tests (host/tests/api_fixtures).
import { readdirSync, readFileSync } from 'node:fs';
import { join } from 'node:path';

export interface FixtureStep {
  method: string;
  path: string;
  body?: unknown;
  auth?: boolean;
}

export interface Fixture {
  name: string;
  setup?: FixtureStep[];
  request: FixtureStep;
  response: { status: number; body?: unknown };
}

// Vitest runs from web/ (jsdom gives import.meta.url an http: scheme, so it can't be used here).
const DIR = join(process.cwd(), '..', 'host', 'tests', 'api_fixtures');

export function loadFixtures(): Map<string, Fixture> {
  const out = new Map<string, Fixture>();
  for (const f of readdirSync(DIR).filter((f) => f.endsWith('.json')).sort()) {
    for (const c of JSON.parse(readFileSync(join(DIR, f), 'utf8')) as Fixture[]) out.set(c.name, c);
  }
  return out;
}

// Turns a response pattern into a concrete value: placeholders become sample values.
export function materialize(pattern: unknown): unknown {
  if (typeof pattern === 'string') {
    switch (pattern) {
      case '<string>':
        return 'sample';
      case '<number>':
        return 0;
      case '<bool>':
        return false;
      case '<null>':
      case '<any>':
        return null;
      case '<array>':
        return [];
      case '<object>':
        return {};
    }
    return pattern;
  }
  if (Array.isArray(pattern)) return pattern.map(materialize);
  if (pattern && typeof pattern === 'object')
    return Object.fromEntries(Object.entries(pattern).map(([k, v]) => [k, materialize(v)]));
  return pattern;
}

export interface RecordedRequest {
  method: string;
  path: string;
  body: unknown;
  auth: string | null;
}

// A fetch that answers with the fixture's response and records what was sent.
export function fixtureFetch(fx: Fixture, sent: RecordedRequest[]): typeof fetch {
  return (async (input: RequestInfo | URL, init?: RequestInit) => {
    const headers = (init?.headers ?? {}) as Record<string, string>;
    sent.push({
      method: init?.method ?? 'GET',
      path: String(input),
      body: init?.body ? JSON.parse(String(init.body)) : undefined,
      auth: headers.Authorization ?? null,
    });
    const body = fx.response.body === undefined ? '' : JSON.stringify(materialize(fx.response.body));
    return new Response(body, { status: fx.response.status, headers: { 'Content-Type': 'application/json' } });
  }) as typeof fetch;
}
