# wrapper

A tool to decrypt Apple Music. An active subscription is required.

Only supports Linux x86_64 and arm64.

# Installation

Download the pre-built binary from this project's [Actions](https://github.com/WorldObservationLog/wrapper/actions).

Alternatively, you can refer to the Actions configuration file for compilation instructions.

# Docker

> **Recommended**: Use [Docker Compose](#docker-compose-recommended) for easier setup and management.

Available for x86_64 and arm64. You need to download the pre-built binary from Releases or Actions.

Build image: `docker build --tag wrapper .`

Login: `docker run -v ./rootfs/data:/app/rootfs/data -p 10020:10020 -e args="-L username:password -F -H 0.0.0.0" wrapper`

Run: `docker run -v ./rootfs/data:/app/rootfs/data -p 10020:10020 -e args="-H 0.0.0.0" wrapper`

## Docker Compose (Recommended)

Docker Compose is the recommended way to run wrapper. It makes it easy to run alongside other services like [gamdl](https://github.com/glomatico/gamdl) for a complete Apple Music downloading setup.

1. Create a `.env` file in the same directory as your `compose.yaml`. You can copy from `.env.example` and edit it:

```zsh
cp .env.example .env
```

```env
# Your Apple ID (email address)
USERNAME=your-apple-id@example.com
# Your Apple ID password
PASSWORD=your-apple-id-password
```

2. Create a `compose.yaml` file:

```yaml
services:
  wrapper:
    container_name: wrapper
    image: wrapper
    volumes:
      - rootfs/data:/app/rootfs/data
    environment:
      USERNAME: $USERNAME
      PASSWORD: $PASSWORD
    restart: unless-stopped

volumes:
  rootfs:
    name: rootfs
    driver: local
```

3. Run the container:

```zsh
docker compose up -d
```

4. **(First time only)** If your Apple ID has Two-Factor Authentication (2FA) enabled, you need to provide the verification code:

```zsh
docker exec -it wrapper sh -c 'echo -n XXXXXX > /app/rootfs/data/data/com.apple.android.music/files/2fa.txt'
```

Replace `XXXXXX` with the 6-digit verification code sent to your device.

> **Note**: An active Apple Music subscription is required.

# Usage

```zsh
Usage: wrapper [OPTION]...

  -h, --help              Print help and exit
  -V, --version           Print version and exit
  -H, --host=STRING         (default=`127.0.0.1')
  -D, --decrypt-port=INT    (default=`10020')
  -M, --m3u8-port=INT       (default=`20020')
  -A, --account-port=INT    (default=`30020')
  -P, --proxy=STRING        (default=`')
  -L, --login=STRING        (username:password)
  -F, --code-from-file      (default=off)
```

# Build from Source

1. Install dependencies:

- Build tools:

  ```zsh
  sudo apt install build-essential cmake wget unzip git
  ```

- LLVM:

  ```zsh
  sudo bash -c "$(wget -O - https://apt.llvm.org/llvm.sh)"
  ```

- Android NDK r23b:

  ```zsh
  wget -O android-ndk-r23b-linux.zip https://dl.google.com/android/repository/android-ndk-r23b-linux.zip
  unzip -q -d ~ android-ndk-r23b-linux.zip
  ```

2. Build:

```zsh
git clone https://github.com/WorldObservationLog/wrapper
cd wrapper
mkdir build
cd build
cmake ..
make -j$(nproc)
```

# Special Thanks
- Anonymous, for providing the original version of this project and the legacy Frida decryption method.
- chocomint, for providing support for arm64 arch.
