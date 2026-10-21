import { useState } from 'react';
import {
  CartesianGrid,
  Line,
  LineChart,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from 'recharts';
import Card from './Card.jsx';
import { formatScientific, powerOfTenTicks } from '../lib/format.js';
import { useThemeColors } from '../lib/useThemeColors.js';

// Smallest value drawn on the log axes. 1 - lambda for the trivial eigenvalue lambda = 1
// (and fully converged residuals) is ~1e-16, i.e. zero up to rounding.
const LOG_FLOOR = 1e-16;

function axisProps(colours) {
  return {
    stroke: colours['--axis'],
    tick: { fill: colours['--text-secondary'], fontSize: 12 },
    tickLine: false,
  };
}

function ChartTooltip({ active, payload, render }) {
  if (!active || !payload?.length) return null;
  return <div className="chart-tooltip">{render(payload)}</div>;
}

// Eigenvalues of M sit just below 1, so plotting lambda itself shows a flat line at 1.
// 1 - lambda (an eigenvalue of the normalized Laplacian I - M) on a log axis shows the gaps.
export function EigenvalueChart({ eigenvalues }) {
  const colours = useThemeColors();
  const data = [...eigenvalues]
    .sort((a, b) => b - a)
    .map((lambda, index) => ({ index: index + 1, lambda, gap: Math.max(LOG_FLOOR, 1 - lambda) }));
  const gaps = data.map((row) => row.gap);

  return (
    <Card title="Eigenvalue Spectrum" icon="📈">
      <p className="chart-subtitle">1 − λ for the eigenpairs computed so far (log scale)</p>
      {data.length === 0 ? (
        <p className="placeholder">Run a segmentation to see the spectrum.</p>
      ) : (
        <ResponsiveContainer width="100%" height={220}>
          <LineChart data={data} margin={{ top: 8, right: 16, bottom: 8, left: 8 }}>
            <CartesianGrid stroke={colours['--grid']} vertical={false} />
            <XAxis dataKey="index" {...axisProps(colours)} label={{ value: 'index i', position: 'insideBottom', offset: -4, fill: colours['--text-secondary'], fontSize: 12 }} />
            <YAxis
              scale="log"
              domain={['dataMin', 'dataMax']}
              ticks={powerOfTenTicks(Math.min(...gaps), Math.max(...gaps))}
              tickFormatter={(value) => formatScientific(value, 0)}
              allowDataOverflow
              width={56}
              {...axisProps(colours)}
            />
            <Tooltip
              content={
                <ChartTooltip
                  render={([point]) => (
                    <>
                      <strong>λ{point.payload.index}</strong> = {point.payload.lambda.toFixed(8)}
                      <br />1 − λ = {formatScientific(1 - point.payload.lambda, 2)}
                    </>
                  )}
                />
              }
            />
            <Line
              type="linear"
              dataKey="gap"
              stroke={colours['--series-1']}
              strokeWidth={2}
              dot={{ r: 4, fill: colours['--series-1'], stroke: colours['--surface-1'], strokeWidth: 2 }}
              activeDot={{ r: 6 }}
              isAnimationActive={false}
            />
          </LineChart>
        </ResponsiveContainer>
      )}
    </Card>
  );
}

// Residual ||Mv - mu v|| after the warm-up (step 0) and after each RQI step, for ONE eigenpair.
// The arrows step through the eigenpairs in Laplacian order (L = I - M, eigenvalue 1 - lambda,
// smallest first). The default is the 2nd: the Fiedler vector, the trivial one being 1st.
export function ConvergenceChart({ eigenvalues, residualHistories }) {
  const colours = useThemeColors();
  const [position, setPosition] = useState(1);

  // Eigenpair indices sorted by lambda, largest first = Laplacian eigenvalue smallest first.
  const order = eigenvalues
    .map((lambda, index) => ({ lambda, index }))
    .sort((a, b) => b.lambda - a.lambda);
  const current = Math.max(0, Math.min(position, order.length - 1)); // stays valid if k shrinks
  const selected = order[current];
  const data = selected
    ? residualHistories[selected.index].map((residual, step) => ({
        step,
        residual: Math.max(LOG_FLOOR, residual),
      }))
    : [];
  const values = data.map((row) => row.residual);

  const toggle = selected && (
    <div className="stepper">
      <button type="button" aria-label="Previous eigenvector" disabled={current === 0}
        onClick={() => setPosition(current - 1)}>‹</button>
      <span>
        Eigenvector {current + 1}
        {current === 1 ? ' (Fiedler)' : ''}
      </span>
      <button type="button" aria-label="Next eigenvector" disabled={current === order.length - 1}
        onClick={() => setPosition(current + 1)}>›</button>
    </div>
  );

  return (
    <Card title="RQI Convergence" icon="📉" aside={toggle}>
      <p className="chart-subtitle">
        {selected
          ? `Residual ‖Mv − μv‖ per RQI step (log scale) · Laplacian eigenvalue 1 − λ = ${formatScientific(1 - selected.lambda, 2)}`
          : 'Residual per RQI step (log scale)'}
      </p>
      {data.length === 0 ? (
        <p className="placeholder">Run a segmentation to see convergence.</p>
      ) : (
        <ResponsiveContainer width="100%" height={220}>
          <LineChart data={data} margin={{ top: 8, right: 16, bottom: 8, left: 8 }}>
            <CartesianGrid stroke={colours['--grid']} vertical={false} />
            <XAxis dataKey="step" {...axisProps(colours)} label={{ value: 'RQI step', position: 'insideBottom', offset: -4, fill: colours['--text-secondary'], fontSize: 12 }} />
            <YAxis
              scale="log"
              domain={['dataMin', 'dataMax']}
              ticks={powerOfTenTicks(Math.min(...values), Math.max(...values)).filter((_, i, all) => all.length < 8 || i % 2 === 0)}
              tickFormatter={(value) => formatScientific(value, 0)}
              allowDataOverflow
              width={56}
              {...axisProps(colours)}
            />
            <Tooltip
              content={
                <ChartTooltip
                  render={([point]) => (
                    <>
                      <strong>step {point.payload.step}</strong>
                      <br />residual = {formatScientific(point.payload.residual, 2)}
                    </>
                  )}
                />
              }
            />
            <Line
              type="linear"
              dataKey="residual"
              stroke={colours['--series-1']}
              strokeWidth={2}
              dot={{ r: 4, fill: colours['--series-1'], stroke: colours['--surface-1'], strokeWidth: 2 }}
              activeDot={{ r: 6 }}
              isAnimationActive={false}
            />
          </LineChart>
        </ResponsiveContainer>
      )}
    </Card>
  );
}
