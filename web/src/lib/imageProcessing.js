// Image processing in the browser, using <canvas>.
//
// A canvas is a pixel buffer the page can draw into. Its 2D context can draw a decoded image
// at any size (drawImage), read the pixels back as RGBA bytes (getImageData), and write
// bytes in (putImageData). We use it to shrink the upload before sending it to the server,
// and to paint the segmentation the server sends back.

// The server builds one graph node per pixel. 160 px on the long side gives ~19k nodes,
// which the eigensolver handles in about 2 s.
export const WORKING_MAX_SIDE = 160;
const THUMBNAIL_MAX_SIDE = 64;

// Same order as the chart palette (categorical slots 1-8), so segment i is always the same colour.
export const SEGMENT_PALETTE = [
  [0x2a, 0x78, 0xd6], // blue
  [0xeb, 0x68, 0x34], // orange
  [0x1b, 0xaf, 0x7a], // aqua
  [0xed, 0xa1, 0x00], // yellow
  [0xe8, 0x7b, 0xa4], // magenta
  [0x00, 0x83, 0x00], // green
  [0x62, 0x50, 0xd6], // violet
  [0xe3, 0x49, 0x48], // red
];

// Size that fits inside maxSide x maxSide and keeps the aspect ratio. Never enlarges.
function fitWithin(width, height, maxSide) {
  const scale = Math.min(1, maxSide / Math.max(width, height));
  return {
    width: Math.max(2, Math.round(width * scale)),
    height: Math.max(2, Math.round(height * scale)),
  };
}

// Draws `bitmap` onto a new canvas of the given size. With high-quality smoothing the browser
// averages neighbouring pixels while shrinking, like the C++ downsample().
function drawScaled(bitmap, width, height) {
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext('2d');
  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = 'high';
  context.drawImage(bitmap, 0, 0, width, height);
  return { canvas, context };
}

// Decodes an uploaded file (JPEG, PNG, WebP, ...) into an ImageBitmap the canvas can draw.
export async function decodeImageFile(file) {
  return createImageBitmap(file);
}

// Shrinks the image to the working size and returns its pixels as packed RGB bytes,
// the format the server expects (row-major, 3 bytes per pixel).
export function toWorkingImage(bitmap) {
  const { width, height } = fitWithin(bitmap.width, bitmap.height, WORKING_MAX_SIDE);
  const { context } = drawScaled(bitmap, width, height);
  const rgba = context.getImageData(0, 0, width, height).data; // 4 bytes per pixel

  const rgb = new Uint8Array(width * height * 3);
  for (let pixel = 0; pixel < width * height; pixel++) {
    rgb[pixel * 3] = rgba[pixel * 4];
    rgb[pixel * 3 + 1] = rgba[pixel * 4 + 1];
    rgb[pixel * 3 + 2] = rgba[pixel * 4 + 2];
    // rgba[pixel * 4 + 3] is alpha; transparent areas are treated like their colour.
  }
  return { width, height, rgb };
}

// A small JPEG preview for the history list, as a data: URL (the image bytes inside a string).
export function makeThumbnail(bitmap) {
  const { width, height } = fitWithin(bitmap.width, bitmap.height, THUMBNAIL_MAX_SIDE);
  const { canvas } = drawScaled(bitmap, width, height);
  return canvas.toDataURL('image/jpeg', 0.7);
}

// Base64 lets binary data travel inside JSON (see src/server/base64.hpp).
// btoa() wants a "binary string" (one char per byte); build it in chunks, since passing
// 50k arguments to String.fromCharCode at once can overflow the call stack.
export function bytesToBase64(bytes) {
  let binary = '';
  const chunkSize = 0x8000;
  for (let start = 0; start < bytes.length; start += chunkSize) {
    binary += String.fromCharCode(...bytes.subarray(start, start + chunkSize));
  }
  return btoa(binary);
}

export function base64ToBytes(text) {
  const binary = atob(text);
  const bytes = new Uint8Array(binary.length);
  for (let i = 0; i < binary.length; i++) {
    bytes[i] = binary.charCodeAt(i);
  }
  return bytes;
}

// Paints the segmentation into `canvas` at the working size; CSS scales it up on screen.
// mode 'mean':    each segment gets the average colour of its pixels (like render_segments in C++)
// mode 'palette': segment i gets SEGMENT_PALETTE[i]
export function paintSegments(canvas, labels, working, mode) {
  const { width, height, rgb } = working;
  const numPixels = width * height;

  let colours = SEGMENT_PALETTE;
  if (mode === 'mean') {
    // Sum the colours of each segment's pixels, then divide by the count.
    const numSegments = Math.max(...labels) + 1;
    const sums = new Float64Array(numSegments * 3);
    const counts = new Uint32Array(numSegments);
    for (let pixel = 0; pixel < numPixels; pixel++) {
      const label = labels[pixel];
      counts[label]++;
      for (let channel = 0; channel < 3; channel++) {
        sums[label * 3 + channel] += rgb[pixel * 3 + channel];
      }
    }
    colours = [];
    for (let label = 0; label < numSegments; label++) {
      const count = Math.max(1, counts[label]);
      colours.push([0, 1, 2].map((channel) => Math.round(sums[label * 3 + channel] / count)));
    }
  }

  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext('2d');
  const output = context.createImageData(width, height);
  for (let pixel = 0; pixel < numPixels; pixel++) {
    const colour = colours[labels[pixel] % colours.length];
    output.data[pixel * 4] = colour[0];
    output.data[pixel * 4 + 1] = colour[1];
    output.data[pixel * 4 + 2] = colour[2];
    output.data[pixel * 4 + 3] = 255; // opaque
  }
  context.putImageData(output, 0, 0);
}

// Draws the original upload into `canvas`, capped at 960 px wide so huge photos stay cheap.
export function paintOriginal(canvas, bitmap) {
  const { width, height } = fitWithin(bitmap.width, bitmap.height, 960);
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext('2d');
  context.imageSmoothingQuality = 'high';
  context.drawImage(bitmap, 0, 0, width, height);
}
