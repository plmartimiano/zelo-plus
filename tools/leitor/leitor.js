// Leitor de EAN-13 para a pagina do Zelo+ (Safari/iPhone e navegadores sem BarcodeDetector).
// Usa somente as partes de codigo de barras 1D da biblioteca ZXing (Apache-2.0).
import EAN13Reader from '@zxing/library/esm/core/oned/EAN13Reader';
import BinaryBitmap from '@zxing/library/esm/core/BinaryBitmap';
import HybridBinarizer from '@zxing/library/esm/core/common/HybridBinarizer';
import RGBLuminanceSource from '@zxing/library/esm/core/RGBLuminanceSource';
import DecodeHintType from '@zxing/library/esm/core/DecodeHintType';
const dicas = new Map();
dicas.set(DecodeHintType.TRY_HARDER, true);
const leitor = new EAN13Reader();
function tentar(cinza, w, h) {
  try {
    const bmp = new BinaryBitmap(new HybridBinarizer(new RGBLuminanceSource(cinza, w, h)));
    return leitor.decode(bmp, dicas).getText();
  } catch (e) { return ''; } finally { leitor.reset(); }
}
function cinzaDe(canvas) {
  const w = canvas.width, h = canvas.height;
  const px = canvas.getContext('2d').getImageData(0, 0, w, h).data;
  const cinza = new Uint8ClampedArray(w * h);
  for (let i = 0, j = 0; j < cinza.length; i += 4, j++) cinza[j] = (px[i] * 77 + px[i + 1] * 150 + px[i + 2] * 29) >> 8;
  return cinza;
}
function girar(canvas, graus) {
  const a = graus * Math.PI / 180, c = Math.abs(Math.cos(a)), s = Math.abs(Math.sin(a));
  const out = document.createElement('canvas');
  out.width = Math.round(canvas.width * c + canvas.height * s); out.height = Math.round(canvas.width * s + canvas.height * c);
  const g = out.getContext('2d'); g.fillStyle = '#fff'; g.fillRect(0, 0, out.width, out.height);
  g.translate(out.width / 2, out.height / 2); g.rotate(a); g.drawImage(canvas, -canvas.width / 2, -canvas.height / 2);
  return out;
}
// canvas: desenho da foto ja reduzida. Tenta a foto como esta e girada em varios angulos
// (o leitor percorre linhas horizontais; o codigo pode estar inclinado ou em pe).
window.lerCodigoZXing = function (canvas) {
  for (const graus of [0, 90, 15, -15, 30, -30, 60, -60, 45, -45]) {
    const c = graus ? girar(canvas, graus) : canvas;
    const r = tentar(cinzaDe(c), c.width, c.height);
    if (r) return r;
  }
  return '';
};
