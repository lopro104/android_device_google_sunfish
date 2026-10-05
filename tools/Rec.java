// Records N seconds from the Android mic path (AudioRecord, 48 kHz mono) and prints levels.
// javac -cp prebuilts/sdk/35/public/android.jar Rec.java && d8 --min-api 30 Rec.class
// adb push classes.dex /data/local/tmp/rec.dex
// adb shell CLASSPATH=/data/local/tmp/rec.dex app_process /system/bin Rec 3
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.os.Looper;
import java.io.FileOutputStream;

public class Rec {
    public static void main(String[] a) throws Exception {
        Looper.prepare();
        int rate = 48000, secs = a.length > 0 ? Integer.parseInt(a[0]) : 3;
        int min = AudioRecord.getMinBufferSize(rate, AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT);
        AudioRecord r = new AudioRecord(MediaRecorder.AudioSource.MIC, rate,
                AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, min * 4);
        System.out.println("state=" + r.getState() + " min=" + min);
        r.startRecording();
        System.out.println("recording=" + r.getRecordingState());
        short[] buf = new short[rate * secs];
        int off = 0;
        while (off < buf.length) {
            int n = r.read(buf, off, Math.min(4800, buf.length - off));
            if (n <= 0) { System.out.println("read=" + n); break; }
            off += n;
        }
        r.stop(); r.release();
        long sq = 0; int peak = 0;
        for (int i = 0; i < off; i++) { int v = Math.abs(buf[i]); peak = Math.max(peak, v); sq += (long) v * v; }
        System.out.println("samples=" + off + " peak=" + peak + " rms=" + (int) Math.sqrt(sq / (double) Math.max(off, 1)));
        FileOutputStream f = new FileOutputStream("/data/local/tmp/rec.raw");
        byte[] b = new byte[off * 2];
        for (int i = 0; i < off; i++) { b[2*i] = (byte) buf[i]; b[2*i+1] = (byte) (buf[i] >> 8); }
        f.write(b); f.close();
    }
}
