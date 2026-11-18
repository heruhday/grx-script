import React, { useEffect, useRef, useState } from 'react';
import { formatUsInput, parseUsNumber } from './numberFormat';

// Text inputs avoid WebView/Windows localizing native number controls.
export default function UsNumberInput({ value, onValueChange, step, ...props }) {
  const numericValue = parseUsNumber(value);
  const [draft, setDraft] = useState(() => formatUsInput(numericValue));
  const lastValueRef = useRef(numericValue);

  useEffect(() => {
    if (!Object.is(numericValue, lastValueRef.current)) {
      lastValueRef.current = numericValue;
      setDraft(formatUsInput(numericValue));
    }
  }, [numericValue]);

  return <input {...props} type="text" inputMode="decimal" lang="en-US"
    data-number-format="en-US" aria-invalid={!Number.isFinite(parseUsNumber(draft))}
    value={draft} title="US number format: 1,234.56"
    onFocus={() => setDraft(Number.isFinite(numericValue) ? String(numericValue) : '')}
    onChange={(event) => {
      const text = event.target.value;
      setDraft(text);
      const number = parseUsNumber(text);
      if (Number.isFinite(number)) {
        lastValueRef.current = number;
        onValueChange(number);
      }
    }}
    onKeyDown={(event) => {
      if (event.key !== 'ArrowUp' && event.key !== 'ArrowDown') return;
      const current = parseUsNumber(draft);
      if (!Number.isFinite(current)) return;
      event.preventDefault();
      const direction = event.key === 'ArrowUp' ? 1 : -1;
      const number = Number((current + direction * (Number(step) || 1)).toPrecision(15));
      lastValueRef.current = number;
      setDraft(String(number));
      onValueChange(number);
    }}
    onBlur={() => {
      const number = parseUsNumber(draft);
      // Incomplete/invalid edits restore the last valid value.
      setDraft(formatUsInput(Number.isFinite(number) ? number : numericValue));
    }} />;
}
