import { humanDuration } from '../format';
import type { FormValues } from '../paramForm';
import type { ParamSpec } from '../types';

// Renders one input per game setting, straight from the game's ParamSchema.
export function ParamFields(props: {
  schema: ParamSpec[];
  values: FormValues;
  errors: Record<string, string>;
  onChange: (key: string, value: string | boolean) => void;
}) {
  const { schema, values, errors, onChange } = props;
  const text = (key: string) => (e: Event) => onChange(key, (e.target as HTMLInputElement).value);
  return (
    <div class="params" data-testid="param-fields">
      {schema.map((s) => (
        <div class="field" key={s.key}>
          <label for={`p-${s.key}`}>{s.label}</label>
          {s.type === 'bool' && (
            <input
              id={`p-${s.key}`}
              type="checkbox"
              checked={Boolean(values[s.key])}
              onChange={(e) => onChange(s.key, (e.target as HTMLInputElement).checked)}
            />
          )}
          {s.type === 'duration' && (
            <div class="duration">
              <input
                id={`p-${s.key}`}
                type="number"
                inputMode="numeric"
                min="0"
                aria-label={`${s.label} minutes`}
                value={String(values[`${s.key}.min`] ?? '')}
                onInput={text(`${s.key}.min`)}
              />
              <span>min</span>
              <input
                type="number"
                inputMode="numeric"
                min="0"
                max="59"
                aria-label={`${s.label} seconds`}
                value={String(values[`${s.key}.sec`] ?? '')}
                onInput={text(`${s.key}.sec`)}
              />
              <span>s</span>
            </div>
          )}
          {(s.type === 'int' || s.type === 'float') && (
            <input
              id={`p-${s.key}`}
              type="number"
              inputMode="decimal"
              step={s.type === 'int' ? '1' : '0.1'}
              min={s.min}
              max={s.max}
              value={String(values[s.key] ?? '')}
              onInput={text(s.key)}
            />
          )}
          {s.type === 'enum' && (
            <select id={`p-${s.key}`} value={String(values[s.key] ?? '')} onChange={text(s.key)}>
              {s.options?.map((o) => (
                <option key={o} value={o}>
                  {o}
                </option>
              ))}
            </select>
          )}
          {s.type === 'string' && (
            <input id={`p-${s.key}`} type="text" maxLength={s.max_length} value={String(values[s.key] ?? '')} onInput={text(s.key)} />
          )}
          {s.help && <small class="muted">{s.help}</small>}
          {s.type === 'duration' && s.max !== undefined && (
            <small class="muted">
              {humanDuration(s.min ?? 0)} to {humanDuration(s.max)}
            </small>
          )}
          {errors[s.key] && (
            <small class="error" role="alert">
              {errors[s.key]}
            </small>
          )}
        </div>
      ))}
    </div>
  );
}
