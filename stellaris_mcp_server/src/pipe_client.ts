import net from "node:net";

const PIPE_PATH = "\\\\.\\pipe\\stellaris_mcp_bridge";

export class PipeClient {
  private socket: net.Socket | null = null;
  private buffer = "";
  private nextId = 1;
  private pendingRequests = new Map<
    number,
    {
      resolve: (val: any) => void;
      reject: (err: Error) => void;
      timeout: NodeJS.Timeout;
    }
  >();

  public async connect(retries = 3, delayMs = 500): Promise<void> {
    for (let attempt = 1; attempt <= retries; attempt++) {
      try {
        await this.tryConnectOnce();
        return;
      } catch (err) {
        if (attempt === retries) {
          throw new Error(
            `Failed to connect to Stellaris Named Pipe (${PIPE_PATH}). Ensure stellaris_bridge.dll is injected into stellaris.exe.`
          );
        }
        await new Promise((r) => setTimeout(r, delayMs));
      }
    }
  }

  private tryConnectOnce(): Promise<void> {
    return new Promise((resolve, reject) => {
      const socket = net.createConnection({ path: PIPE_PATH }, () => {
        this.socket = socket;
        this.setupSocketHandlers();
        resolve();
      });

      socket.once("error", (err) => {
        socket.destroy();
        reject(err);
      });
    });
  }

  private setupSocketHandlers(): void {
    if (!this.socket) return;

    this.socket.on("data", (chunk) => {
      this.buffer += chunk.toString("utf-8");

      let newlineIndex: number;
      while ((newlineIndex = this.buffer.indexOf("\n")) !== -1) {
        const line = this.buffer.slice(0, newlineIndex).trim();
        this.buffer = this.buffer.slice(newlineIndex + 1);

        if (!line) continue;

        try {
          const resp = JSON.parse(line);
          this.handleResponse(resp);
        } catch (e) {
          console.error("[PipeClient] Malformed response:", line, e);
        }
      }
    });

    this.socket.on("close", () => {
      this.cleanupPending(new Error("Pipe connection closed by Stellaris Bridge."));
      this.socket = null;
    });

    this.socket.on("error", (err) => {
      console.error("[PipeClient] Socket error:", err.message);
    });
  }

  private handleResponse(resp: any): void {
    const id = resp.id;
    if (typeof id !== "number") return;

    const pending = this.pendingRequests.get(id);
    if (!pending) return;

    clearTimeout(pending.timeout);
    this.pendingRequests.delete(id);

    if (resp.error) {
      pending.reject(
        new Error(
          `Stellaris Bridge Error (${resp.error.code}): ${resp.error.message}`
        )
      );
    } else {
      pending.resolve(resp.result);
    }
  }

  private cleanupPending(err: Error): void {
    for (const [, pending] of this.pendingRequests) {
      clearTimeout(pending.timeout);
      pending.reject(err);
    }
    this.pendingRequests.clear();
  }

  public async request(method: string, params: Record<string, any> = {}): Promise<any> {
    if (!this.socket || this.socket.destroyed) {
      await this.connect();
    }

    const id = this.nextId++;
    const req = {
      jsonrpc: "2.0",
      method,
      params,
      id,
    };

    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pendingRequests.delete(id);
        reject(
          new Error(
            `Request '${method}' timed out waiting for Stellaris main thread response.`
          )
        );
      }, 3000);

      this.pendingRequests.set(id, { resolve, reject, timeout });

      const payload = JSON.stringify(req) + "\n";
      this.socket!.write(payload, "utf-8", (err) => {
        if (err) {
          clearTimeout(timeout);
          this.pendingRequests.delete(id);
          reject(err);
        }
      });
    });
  }

  public close(): void {
    if (this.socket) {
      this.socket.destroy();
      this.socket = null;
    }
  }
}
