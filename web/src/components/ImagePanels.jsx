import { useEffect, useRef } from 'react';
import Card from './Card.jsx';
import { paintOriginal, paintSegments } from '../lib/imageProcessing.js';

// React renders the <canvas> element; drawing into it has to happen afterwards, once the
// element exists in the page. useRef gives us a handle to the element, and useEffect runs
// after rendering. Its dependency array ([bitmap]) means "draw again whenever bitmap changes".

export function OriginalImage({ image }) {
  const canvasRef = useRef(null);

  useEffect(() => {
    if (image) paintOriginal(canvasRef.current, image.bitmap);
  }, [image]);

  return (
    <Card
      title="Original Image"
      icon="🖼"
      aside={image && <span className="value-chip">{image.bitmap.width} × {image.bitmap.height}</span>}
    >
      <div className="image-frame">
        {image ? <canvas ref={canvasRef} /> : <p className="placeholder">Upload an image to start.</p>}
      </div>
    </Card>
  );
}

export function SegmentedImage({ image, labels, displayMode, busy }) {
  const canvasRef = useRef(null);

  useEffect(() => {
    if (image && labels) paintSegments(canvasRef.current, labels, image.working, displayMode);
  }, [image, labels, displayMode]);

  const showCanvas = image && labels;
  return (
    <Card
      title="Segmented Output"
      icon="◎"
      aside={image && <span className="value-chip">{image.working.width} × {image.working.height}</span>}
    >
      <div className={`image-frame ${busy ? 'is-busy' : ''}`}>
        {showCanvas ? (
          <canvas ref={canvasRef} className="pixelated" />
        ) : (
          <p className="placeholder">{busy ? 'Segmenting…' : 'The segmentation appears here.'}</p>
        )}
      </div>
    </Card>
  );
}
