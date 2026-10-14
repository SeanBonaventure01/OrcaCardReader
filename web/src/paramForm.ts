import type { ParamSpec } from './types';

// Converts between a game's ParamSchema values and form field strings. Durations are edited
// as minutes and seconds; everything is validated like the firmware does before sending.

export type FormValues = Record<string, string | boolean>;

export function durationParts(ms: number): { min: string; sec: string } {
  const total = Math.round(ms / 1000);
  return { min: String(Math.floor(total / 60)), sec: String(total % 60) };
}

export function toFormValues(schema: ParamSpec[], params: Record<string, unknown> = {}): FormValues {
  const v: FormValues = {};
  for (const s of schema) {
    const value = s.key in params ? params[s.key] : s.default;
    if (s.type === 'bool') v[s.key] = Boolean(value);
    else if (s.type === 'duration') {
      const p = durationParts(Number(value));
      v[`${s.key}.min`] = p.min;
      v[`${s.key}.sec`] = p.sec;
    } else v[s.key] = String(value ?? '');
  }
  return v;
}

export interface FormResult {
  params: Record<string, unknown>;
  errors: Record<string, string>;
}

function rangeError(s: ParamSpec, shown: (n: number) => string): string {
  return `Must be between ${shown(s.min ?? 0)} and ${shown(s.max ?? 0)}`;
}

export function fromFormValues(schema: ParamSpec[], v: FormValues): FormResult {
  const params: Record<string, unknown> = {};
  const errors: Record<string, string> = {};
  for (const s of schema) {
    switch (s.type) {
      case 'bool':
        params[s.key] = Boolean(v[s.key]);
        break;
      case 'duration': {
        const minS = String(v[`${s.key}.min`] ?? '0').trim() || '0';
        const secS = String(v[`${s.key}.sec`] ?? '0').trim() || '0';
        if (!/^\d+$/.test(minS) || !/^\d+$/.test(secS)) {
          errors[s.key] = 'Use whole minutes and seconds';
          break;
        }
        const ms = (Number(minS) * 60 + Number(secS)) * 1000;
        if (ms < (s.min ?? 0) || ms > (s.max ?? Infinity)) {
          errors[s.key] = rangeError(s, (n) => {
            const p = durationParts(n);
            return `${p.min}:${p.sec.padStart(2, '0')}`;
          });
          break;
        }
        params[s.key] = ms;
        break;
      }
      case 'int':
      case 'float': {
        const raw = String(v[s.key] ?? '').trim();
        const n = Number(raw);
        if (raw === '' || !Number.isFinite(n) || (s.type === 'int' && !Number.isInteger(n))) {
          errors[s.key] = s.type === 'int' ? 'Must be a whole number' : 'Must be a number';
          break;
        }
        if (n < (s.min ?? -Infinity) || n > (s.max ?? Infinity)) {
          errors[s.key] = rangeError(s, String);
          break;
        }
        params[s.key] = n;
        break;
      }
      case 'enum': {
        const val = String(v[s.key] ?? '');
        if (!s.options?.includes(val)) errors[s.key] = 'Pick one of the options';
        else params[s.key] = val;
        break;
      }
      case 'string': {
        const val = String(v[s.key] ?? '');
        if (val.length > (s.max_length ?? 64)) errors[s.key] = `At most ${s.max_length ?? 64} characters`;
        else params[s.key] = val;
        break;
      }
    }
  }
  return { params, errors };
}
