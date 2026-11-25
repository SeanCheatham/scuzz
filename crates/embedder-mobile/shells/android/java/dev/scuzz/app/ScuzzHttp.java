package dev.scuzz.app;

import java.io.ByteArrayOutputStream;
import java.io.Closeable;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.reflect.Field;
import java.net.HttpURLConnection;
import java.net.InetAddress;
import java.net.ProtocolException;
import java.net.URL;
import java.nio.ByteBuffer;
import java.nio.CharBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CharsetDecoder;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.SynchronousQueue;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

/** Platform HTTP client transport for Android Net calls using HttpURLConnection. */
public final class ScuzzHttp {
  /* Keep this equal to SZ_HTTP_BODY_MAX. */
  private static final int HTTP_BODY_MAX = 2 * 1024 * 1024;
  private static final int HTTP_HEADERS_MAX = 16384;

  private static final ExecutorService EXECUTOR =
      new ThreadPoolExecutor(
          0,
          Integer.MAX_VALUE,
          5L,
          TimeUnit.SECONDS,
          new SynchronousQueue<Runnable>(),
          new ThreadFactory() {
            @Override
            public Thread newThread(Runnable task) {
              Thread thread = new Thread(task, "scuzz-http");
              thread.setDaemon(true);
              return thread;
            }
          });

  private ScuzzHttp() {}

  private static native boolean nativeWriteChunk(long handle, byte[] chunk, int length);

  private static native void nativeComplete(
      long handle, int status, byte[] headers, byte[] body, String failure);

  public static final class Call {
    volatile boolean cancelled;
    private HttpURLConnection connection;
    private InputStream input;
    private OutputStream output;
    private Thread worker;

    synchronized boolean bindWorker(Thread current) {
      if (cancelled) return false;
      worker = current;
      return true;
    }

    synchronized boolean bindConnection(HttpURLConnection conn) {
      if (cancelled) {
        conn.disconnect();
        return false;
      }
      connection = conn;
      return true;
    }

    synchronized boolean bindOutput(OutputStream stream) {
      if (cancelled) {
        closeQuietly(stream);
        return false;
      }
      output = stream;
      return true;
    }

    synchronized boolean bindInput(InputStream stream) {
      if (cancelled) {
        closeQuietly(stream);
        return false;
      }
      input = stream;
      return true;
    }

    synchronized void clearResources() {
      input = null;
      output = null;
      connection = null;
      worker = null;
    }

    void cancel() {
      HttpURLConnection conn;
      InputStream in;
      OutputStream out;
      Thread thread;
      synchronized (this) {
        cancelled = true;
        conn = connection;
        in = input;
        out = output;
        thread = worker;
        connection = null;
        input = null;
        output = null;
        worker = null;
      }
      closeQuietly(in);
      closeQuietly(out);
      if (conn != null) {
        try {
          conn.disconnect();
        } catch (Throwable ignored) {
        }
      }
      if (thread != null) {
        thread.interrupt();
      }
    }
  }

  public static Object start(
      final long handle,
      final String method,
      final byte[] urlBytes,
      final byte[] headersBlob,
      final byte[] body,
      final boolean streamToFile,
      final int timeoutMs) {
    final Call call = new Call();
    EXECUTOR.execute(
        new Runnable() {
          @Override
          public void run() {
            execute(call, handle, method, urlBytes, headersBlob, body, streamToFile, timeoutMs);
          }
        });
    return call;
  }

  public static void cancel(Object rawCall) {
    if (rawCall instanceof Call) {
      ((Call) rawCall).cancel();
    }
  }

  private static void closeQuietly(Closeable resource) {
    if (resource == null) return;
    try {
      resource.close();
    } catch (Throwable ignored) {
    }
  }

  private static String decodeUtf8(byte[] bytes) throws CharacterCodingException {
    if (bytes == null || bytes.length == 0) return "";
    CharsetDecoder decoder =
        StandardCharsets.UTF_8
            .newDecoder()
            .onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT);
    CharBuffer chars = decoder.decode(ByteBuffer.wrap(bytes));
    return chars.toString();
  }

  private static int readInt32(byte[] data, int offset) {
    return ((data[offset] & 0xff) << 24)
        | ((data[offset + 1] & 0xff) << 16)
        | ((data[offset + 2] & 0xff) << 8)
        | (data[offset + 3] & 0xff);
  }

  private static void writeInt32(ByteArrayOutputStream out, int value) {
    out.write((value >>> 24) & 0xff);
    out.write((value >>> 16) & 0xff);
    out.write((value >>> 8) & 0xff);
    out.write(value & 0xff);
  }

  private static Map<String, String> decodeHeaders(byte[] blob) throws CharacterCodingException {
    Map<String, String> result = new LinkedHashMap<String, String>();
    if (blob == null || blob.length == 0) return result;
    int offset = 0;
    while (offset < blob.length) {
      if (blob.length - offset < 4) throw new CharacterCodingException();
      int nameLen = readInt32(blob, offset);
      offset += 4;
      if (nameLen < 0 || blob.length - offset < nameLen + 4) throw new CharacterCodingException();
      byte[] nameBytes = new byte[nameLen];
      System.arraycopy(blob, offset, nameBytes, 0, nameLen);
      offset += nameLen;
      int valLen = readInt32(blob, offset);
      offset += 4;
      if (valLen < 0 || blob.length - offset < valLen) throw new CharacterCodingException();
      byte[] valBytes = new byte[valLen];
      System.arraycopy(blob, offset, valBytes, 0, valLen);
      offset += valLen;
      result.put(decodeUtf8(nameBytes), decodeUtf8(valBytes));
    }
    return result;
  }

  private static String trimHttpWhitespace(String text) {
    int start = 0;
    int end = text.length();
    while (start < end && (text.charAt(start) == ' ' || text.charAt(start) == '\t')) start++;
    while (end > start && (text.charAt(end - 1) == ' ' || text.charAt(end - 1) == '\t')) end--;
    return text.substring(start, end);
  }

  private static boolean isUniqueLocalIpv6(InetAddress address) {
    byte[] raw = address.getAddress();
    return raw != null && raw.length == 16 && (raw[0] & 0xfe) == 0xfc;
  }

  private static boolean isLocalAddress(InetAddress address) {
    return address.isLoopbackAddress()
        || address.isSiteLocalAddress()
        || address.isLinkLocalAddress()
        || isUniqueLocalIpv6(address);
  }

  public static boolean isLocalNetworkHost(String rawHost) throws IOException {
    if (rawHost == null || rawHost.isEmpty()) return false;
    String host = rawHost;
    if (host.startsWith("[") && host.endsWith("]") && host.length() > 2) {
      host = host.substring(1, host.length() - 1);
    }
    String lower = host.toLowerCase(Locale.ROOT);
    if ("localhost".equals(lower) || lower.endsWith(".local")) return true;
    if (lower.indexOf('.') < 0 && lower.indexOf(':') < 0) return true;
    InetAddress[] addresses = InetAddress.getAllByName(host);
    if (addresses == null || addresses.length == 0) return false;
    for (InetAddress address : addresses) {
      if (!isLocalAddress(address)) return false;
    }
    return true;
  }

  private static void setMethod(HttpURLConnection conn, String method) throws Exception {
    try {
      conn.setRequestMethod(method);
    } catch (ProtocolException error) {
      if (!"PATCH".equals(method)) throw error;
      for (Class<?> cls = conn.getClass(); cls != null; cls = cls.getSuperclass()) {
        try {
          Field delegateField = cls.getDeclaredField("delegate");
          delegateField.setAccessible(true);
          Object delegate = delegateField.get(conn);
          if (delegate instanceof HttpURLConnection) {
            setMethod((HttpURLConnection) delegate, method);
          }
        } catch (NoSuchFieldException ignored) {
        }
      }
      Field methodField = HttpURLConnection.class.getDeclaredField("method");
      methodField.setAccessible(true);
      methodField.set(conn, method);
    }
  }

  private static long parseContentLength(HttpURLConnection conn) throws IOException {
    String header = conn.getHeaderField("Content-Length");
    if (header == null) return -1L;
    String trimmed = trimHttpWhitespace(header);
    if (trimmed.isEmpty()) return -1L;
    try {
      long length = Long.parseLong(trimmed);
      if (length < 0L) throw new IOException("malformed Content-Length");
      return length;
    } catch (NumberFormatException error) {
      throw new IOException("malformed Content-Length");
    }
  }

  private static void execute(
      Call call,
      long handle,
      String method,
      byte[] urlBytes,
      byte[] headersBlob,
      byte[] body,
      boolean streamToFile,
      int timeoutMs) {
    HttpURLConnection conn = null;
    InputStream in = null;
    OutputStream out = null;
    int status = 0;
    byte[] encodedHeaders = null;
    byte[] responseBody = null;
    String failure = null;

    try {
      if (!call.bindWorker(Thread.currentThread())) {
        failure = "cancelled";
        return;
      }
      String urlText;
      try {
        urlText = decodeUtf8(urlBytes);
      } catch (CharacterCodingException error) {
        failure = "invalid URL";
        return;
      }
      Map<String, String> requestHeaders;
      try {
        requestHeaders = decodeHeaders(headersBlob);
      } catch (CharacterCodingException error) {
        failure = "invalid UTF-8 request headers";
        return;
      }

      @SuppressWarnings("deprecation")
      URL target = new URL(urlText);
      String protocol = target.getProtocol();
      if (!"http".equalsIgnoreCase(protocol) && !"https".equalsIgnoreCase(protocol)) {
        failure = "invalid URL";
        return;
      }
      if ("http".equalsIgnoreCase(protocol) && !isLocalNetworkHost(target.getHost())) {
        failure = "cleartext HTTP requires a local network host";
        return;
      }

      conn = (HttpURLConnection) target.openConnection();
      if (!call.bindConnection(conn)) {
        conn = null;
        failure = "cancelled";
        return;
      }
      conn.setInstanceFollowRedirects(false);
      conn.setUseCaches(false);
      conn.setAllowUserInteraction(false);
      int wait = timeoutMs > 0 ? timeoutMs : 30000;
      conn.setConnectTimeout(wait);
      conn.setReadTimeout(wait);

      String wireMethod = streamToFile ? "GET" : (method != null ? method : "GET");
      setMethod(conn, wireMethod);
      conn.setRequestProperty("Accept-Encoding", "identity");
      conn.setRequestProperty("Connection", "close");
      for (Map.Entry<String, String> entry : requestHeaders.entrySet()) {
        conn.setRequestProperty(entry.getKey(), entry.getValue());
      }

      if (!streamToFile && body != null) {
        conn.setDoOutput(true);
        conn.setFixedLengthStreamingMode(body.length);
        out = conn.getOutputStream();
        if (!call.bindOutput(out)) {
          out = null;
          failure = "cancelled";
          return;
        }
        if (body.length > 0) {
          out.write(body);
        }
        out.flush();
        closeQuietly(out);
        out = null;
      }

      status = conn.getResponseCode();
      if (status < 100) {
        failure = "invalid HTTP response";
        return;
      }

      Map<String, List<String>> rawHeaders = conn.getHeaderFields();
      Map<String, String> normalized = new LinkedHashMap<String, String>();
      long headerSize = 0;
      if (rawHeaders != null) {
        for (Map.Entry<String, List<String>> entry : rawHeaders.entrySet()) {
          String rawName = entry.getKey();
          if (rawName == null) continue;
          List<String> values = entry.getValue();
          if (values == null) continue;
          String lowerName = rawName.toLowerCase(Locale.ROOT);
          byte[] nameBytes = lowerName.getBytes(StandardCharsets.UTF_8);
          for (String rawVal : values) {
            if (rawVal == null) continue;
            String val = trimHttpWhitespace(rawVal);
            byte[] valBytes = val.getBytes(StandardCharsets.UTF_8);
            headerSize += nameBytes.length + valBytes.length + 4L;
            if (headerSize > HTTP_HEADERS_MAX) {
              failure = "response headers too large";
              return;
            }
            String existing = normalized.get(lowerName);
            normalized.put(lowerName, existing == null ? val : existing + ", " + val);
          }
        }
      }

      ByteArrayOutputStream headerStream = new ByteArrayOutputStream();
      for (Map.Entry<String, String> entry : normalized.entrySet()) {
        byte[] k = entry.getKey().getBytes(StandardCharsets.UTF_8);
        byte[] v = entry.getValue().getBytes(StandardCharsets.UTF_8);
        writeInt32(headerStream, k.length);
        headerStream.write(k, 0, k.length);
        writeInt32(headerStream, v.length);
        headerStream.write(v, 0, v.length);
      }
      encodedHeaders = headerStream.toByteArray();

      long expectedLength = parseContentLength(conn);
      if (!streamToFile && !"HEAD".equals(wireMethod) && expectedLength > HTTP_BODY_MAX) {
        failure = "response body too large";
        return;
      }

      if (!streamToFile && "HEAD".equals(wireMethod)) {
        responseBody = new byte[0];
        return;
      }

      in = status >= 400 ? conn.getErrorStream() : conn.getInputStream();
      if (in != null && !call.bindInput(in)) {
        in = null;
        failure = "cancelled";
        return;
      }

      if (streamToFile) {
        long written = 0L;
        if (in != null) {
          byte[] buffer = new byte[16384];
          int count;
          while ((count = in.read(buffer)) != -1) {
            if (count == 0) continue;
            if (!nativeWriteChunk(handle, buffer, count)) {
              failure = "cannot write output file";
              return;
            }
            written += count;
            if (expectedLength >= 0L && written > expectedLength) {
              failure = "response exceeds content length";
              return;
            }
          }
        }
        if (expectedLength >= 0L && written != expectedLength) {
          failure = "incomplete HTTP response";
        }
      } else {
        ByteArrayOutputStream bufferOut = new ByteArrayOutputStream();
        if (in != null) {
          byte[] buffer = new byte[8192];
          int count;
          while ((count = in.read(buffer)) != -1) {
            if (count == 0) continue;
            if (count > HTTP_BODY_MAX - bufferOut.size()) {
              failure = "response body too large";
              return;
            }
            bufferOut.write(buffer, 0, count);
          }
        }
        if (expectedLength >= 0L && bufferOut.size() != expectedLength) {
          failure = "incomplete HTTP response";
          return;
        }
        responseBody = bufferOut.toByteArray();
      }
    } catch (Throwable error) {
      String detail = error.getMessage();
      failure = (detail != null && !detail.isEmpty()) ? detail : error.getClass().getSimpleName();
    } finally {
      closeQuietly(in);
      closeQuietly(out);
      call.clearResources();
      if (conn != null) {
        try {
          conn.disconnect();
        } catch (Throwable ignored) {
        }
      }
      nativeComplete(handle, status, encodedHeaders, responseBody, failure);
    }
  }
}
