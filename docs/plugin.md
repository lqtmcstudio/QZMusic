# QZMusic v2 插件开发文档

> 适用于当前 QZ Music Android 插件系统。插件是一个单文件 CommonJS 模块，安装后会保存在应用私有目录 `files/plugins/<pluginId>/index.js`，运行时由 Javet Node.js V8 执行。

## 1. 插件文件

插件发布文件就是一个 `index.js`。用户在应用内导入该 JS 文件后，QZ Music 会：

1. 将文件临时保存为插件目录下的 `index.js`；
2. 执行 `require('./index.js')`；
3. 读取导出的 `pluginInfo`；
4. 把 `pluginInfo` 写入 `plugin.json`；
5. 将插件目录重命名为 `pluginInfo.info.id`。

插件 ID 规则：

- `info.id` 必填，不能留空；
- 不能为 `local`；
- 不能以 `_temp_` 开头；
- 若同 ID 插件已存在，当前实现会安装失败，需要用户先卸载旧版本。

## 2. 最小插件模板

```js
const pluginInfo = {
  info: {
    id: "demo",
    name: "Demo Music",
    description: "QZMusic v2 插件示例",
    version: "1.0.0"
  },
  supportFunc: [
    "search_song",
    "search_playlist",
    "playlist",
    "album"
  ],
  quality: [
    { id: "128k", name: "标准音质", ui: "128K" },
    { id: "320k", name: "高音质", ui: "320K" }
  ],
  env: [
    {
      key: "cookie",
      name: "Cookie",
      description: "可选：登录态 Cookie"
    }
  ],
  ext: [
    {
      name: "热门歌单",
      description: "查看 Demo 热门歌单",
      entry: "plugin.songList.getHotLists(30)",
      type: "playlists"
    }
  ]
}

async function requestJson(url, options = {}) {
  const res = await fetch(url, options)
  if (!res.ok) throw new Error(`HTTP ${res.status}`)
  return await res.json()
}

const plugin = {
  pluginInfo,

  musicSearch: {
    async search(keyword, page = 1) {
      return {
        list: [],
        page,
        limit: 30,
        total: 0,
        has_more: false
      }
    }
  },

  songList: {
    async getHotLists(limit = 10) {
      return []
    },

    async getListDetail(id, page = 1, limit = 30) {
      return {
        id,
        source: pluginInfo.info.id,
        page,
        limit,
        total: 0,
        info: {
          name: "空歌单",
          img: "",
          desc: "",
          author: ""
        },
        list: []
      }
    }
  },

  album: {
    async getListDetail(id) {
      return {
        source: pluginInfo.info.id,
        total: 0,
        info: {
          id,
          name: "未知专辑",
          artist: "未知",
          img: "",
          description: ""
        },
        list: []
      }
    }
  },

  async getUrl(songId, quality) {
    return "msg:请在插件中实现 getUrl"
  },

  async getLyric(songId) {
    return "[00:00.00] 暂无歌词"
  },

  hotSearch: {
    async getList() {
      return []
    }
  }
}

module.exports = plugin
```

## 3. 运行环境

每次插件调用都会创建一个新的 Node Runtime，并执行应用生成的 `main.js`：

```js
const plugin = require('./index.js')
global.env = { /* 用户在插件设置页保存的配置 */ }
autoHandle(plugin.xxx())
```

注意：

- 不要依赖全局内存持久化；每次调用都是新运行时。
- 插件返回值可以是普通值或 Promise。
- 返回对象/数组会被 `JSON.stringify()` 后交给 Kotlin 解析。
- 返回字符串会原样返回。
- 返回以 `msg:` 开头的字符串时，QZ Music 会弹出 toast。
- 单次调用当前等待上限为约 20 秒，网络请求务必设置合理超时。

内置全局对象：

```js
console.log(...args)       // 输出到 Android Logcat：QZPlugin LOG
global.env                 // 插件设置页保存的键值
QZMusic.file.write(path, base64Content)
QZMusic.file.read(path)    // 当前实现返回空字符串，暂不建议依赖
```

`QZMusic.file.write()` 的 `content` 必须是 Base64 字符串，Android 端会解码后写入文件。

## 4. pluginInfo 结构

```ts
type PluginInfoRoot = {
  info: {
    id: string
    name: string
    description: string
    version: string
  }
  ext: PluginExt[]
  env: PluginEnv[]
  quality: PluginQuality[]
  supportFunc: string[]
}

type PluginExt = {
  name: string
  description: string
  entry: string
  type?: "playlists" | "playlist"
}

type PluginEnv = {
  key: string
  name: string
  description: string
}

type PluginQuality = {
  id: string
  name: string
  ui: string
}
```

### supportFunc

当前 manifest 反序列化会保留以下值：

```txt
search_song
search_album
search_lyric
search_playlist
search_artist
playlist
album
artist
```

说明：`getUrl`、`getLyric`、`hotSearch` 会被运行时直接调用，但当前不在 `supportFunc` 白名单内；需要实现时直接在导出的 `plugin` 对象上提供对应函数即可。

### quality

`quality` 决定歌曲可选音质以及自动选择顺序。歌曲对象里的 `qualities` 使用同样的 `id` 作为 key。

```js
quality: [
  { id: "128k", name: "标准音质", ui: "128K" },
  { id: "320k", name: "高音质", ui: "320K" },
  { id: "flac", name: "无损", ui: "FLAC" }
]
```

## 5. 歌曲模型 Music

搜索、歌单详情、专辑详情中的歌曲都使用同一个结构：

```ts
type Music = {
  id: string
  name: string
  artists: string
  source?: string
  pic?: string
  sPic?: string
  mPic?: string
  albumName?: string
  albumId?: string
  interval?: string
  qualities?: Record<string, string>
  quality?: string
  playCount?: number
  extra?: any
}
```

推荐最小字段：

```js
{
  id: "123456",
  name: "歌曲名",
  artists: "歌手 A、歌手 B",
  source: "demo",
  pic: "https://example.com/cover.jpg",
  mPic: "https://example.com/cover.jpg",
  albumName: "专辑名",
  albumId: "album_1",
  interval: "03:45",
  qualities: {
    "128k": "3.4 MB",
    "320k": "8.5 MB"
  }
}
```

`source` 应与 `pluginInfo.info.id` 保持一致。播放时 QZ Music 会生成逻辑 URI：

```txt
qzmusic://music/<source>/<songId>?q=<qualityId>
```

随后调用：

```js
plugin.getUrl(songId, qualityId)
```

## 6. 必要运行时 API

### 6.1 搜索歌曲

调用：

```js
plugin.musicSearch.search(keyword, page)
```

返回：

```ts
type MusicSearchResult = {
  list: Music[]
  page?: number
  limit?: number
  total?: number
  has_more?: boolean
}
```

示例：

```js
musicSearch: {
  async search(keyword, page = 1) {
    const list = await searchSongsFromYourApi(keyword, page)
    return {
      list,
      page,
      limit: 30,
      total: 300,
      has_more: page < 10
    }
  }
}
```

### 6.2 获取播放 URL

调用：

```js
plugin.getUrl(songId, qualityId)
```

返回：

```ts
type PlayUrl = string
```

要求：

- 成功时返回可直接播放的 `http`/`https` URL；
- 失败时可返回 `msg:错误说明`，应用会 toast；
- URL 会按 `source:id:quality` 缓存，源站链接过期时插件侧应返回新的 URL。

示例：

```js
async function getUrl(songId, quality) {
  const data = await requestJson(`https://api.example.com/song/url?id=${songId}&q=${quality}`)
  if (!data.url) return "msg:无法获取播放链接"
  return data.url
}
```

### 6.3 获取歌词

调用：

```js
plugin.getLyric(songId)
```

返回：

```ts
type LyricText = string
```

支持返回 LRC、增强 LRC、KRC/QRC/YRC/TTML 等应用已有解析器能识别的文本。最简单可返回：

```txt
[00:00.00] 歌词第一行
[00:12.34] 歌词第二行
```

无歌词时建议返回：

```js
"[00:00.00] 暂无歌词"
```

### 6.4 热搜

调用：

```js
plugin.hotSearch.getList()
```

返回字符串数组：

```js
["周杰伦", "林俊杰", "Taylor Swift"]
```

## 7. 歌单 API

### 7.1 首页热门歌单/榜单

调用：

```js
plugin.songList.getHotLists(10)
plugin.leaderboard.getBoards()
```

返回数组，每项结构：

```ts
type RemotePlaylistItem = {
  id: string
  name: string
  img: string
  desc?: string
  play_count?: string
}
```

注意：当前首页 `RemotePlaylist.fromJson()` 会把 `source` 固定为 `"wy"`，这是现有实现限制；插件拓展页的 `type: "playlists"` 列表则会使用当前插件 ID 作为 source。

### 7.2 歌单列表拓展

`pluginInfo.ext` 中配置：

```js
{
  name: "编辑精选",
  description: "查看编辑精选歌单",
  entry: "plugin.songList.getHotLists(30)",
  type: "playlists"
}
```

应用会直接执行 `entry`，要求返回数组：

```ts
type UIPlaylistItem = {
  id: string
  name?: string
  desc?: string
  cover?: string
  source?: string
}
```

### 7.3 歌单详情

调用：

```js
plugin.songList.getListDetail(id, page, limit)
```

返回：

```ts
type PlaylistDetail = {
  id?: string
  source?: string
  page?: number
  limit?: number
  total?: number
  user?: string
  owner?: {
    id?: string
    username?: string
    nickname?: string
    avatar?: string
  }
  info?: {
    name?: string
    img?: string
    cover_mode?: "auto" | string
    desc?: string
    author?: string
    author_id?: string
    play_count?: string
    visit_count?: number
    is_public?: boolean
  }
  list: Music[]
}
```

示例：

```js
songList: {
  async getListDetail(id, page = 1, limit = 30) {
    return {
      id,
      source: pluginInfo.info.id,
      page,
      limit,
      total: 100,
      info: {
        name: "歌单标题",
        img: "https://example.com/cover.jpg",
        desc: "歌单简介",
        author: "创建者"
      },
      list: []
    }
  }
}
```

### 7.4 直接打开歌单详情拓展

`pluginInfo.ext` 中配置：

```js
{
  name: "每日推荐",
  description: "直接打开一个动态歌单",
  entry: "plugin.recommend.daily()",
  type: "playlist"
}
```

应用会直接执行 `entry`，要求返回完整 `PlaylistDetail`。

## 8. 专辑 API

调用：

```js
plugin.album.getListDetail(albumId)
```

返回：

```ts
type AlbumDetail = {
  source?: string
  total?: number
  info?: {
    id?: string
    name?: string
    artist?: string
    img?: string
    description?: string
  }
  list: Music[]
}
```

示例：

```js
album: {
  async getListDetail(id) {
    return {
      source: pluginInfo.info.id,
      total: 12,
      info: {
        id,
        name: "专辑名",
        artist: "歌手",
        img: "https://example.com/album.jpg",
        description: "专辑介绍"
      },
      list: []
    }
  }
}
```

## 9. 艺人 API

`supportFunc` 中存在 `artist`、`search_artist`，但当前 UI 主要接入本地艺人详情，远程艺人详情调用路径尚未完整接入。插件可以预留结构：

```js
artist: {
  async getDetail(id) {
    return {
      id,
      name: "艺人名",
      pic: "",
      desc: "",
      musics: [],
      albums: []
    }
  }
}
```

## 10. 安全与转义建议

当前 Kotlin 调用插件时会把部分用户输入拼入 JavaScript 表达式，例如：

```js
plugin.musicSearch.search(`用户输入`, 1)
```

插件作者仍应自行处理：

- URL 参数必须使用 `encodeURIComponent()`；
- Cookie、token 等放在 `global.env`，不要硬编码在公开插件中；
- 不要把用户输入拼进源站 SQL/脚本类接口；
- 网络异常要捕获并返回清晰错误，例如 `msg:网络异常`。

## 11. 调试建议

1. 先保证 `pluginInfo` 可被读取，否则安装会失败。
2. 使用 `console.log()` 输出关键参数和返回值，可在 Logcat 查看 `QZPlugin LOG`。
3. 每个 API 先返回静态 JSON，确认 UI 能展示，再接入真实网络。
4. 搜索返回值必须是 `{ list: [...] }`，不能直接返回歌曲数组。
5. 歌单列表拓展 `type: "playlists"` 返回数组；歌单详情和 `type: "playlist"` 返回完整 `Playlist` 对象。
6. `source`、`quality.id`、歌曲 `qualities` 的 key 要互相匹配，否则自动音质选择和播放 URL 解析会失败。

## 12. 完整返回示例

```js
const demoSong = {
  id: "song_1",
  name: "示例歌曲",
  artists: "示例歌手",
  source: "demo",
  pic: "https://example.com/cover.jpg",
  mPic: "https://example.com/cover.jpg",
  albumName: "示例专辑",
  albumId: "album_1",
  interval: "03:30",
  qualities: {
    "128k": "3 MB",
    "320k": "8 MB"
  }
}

module.exports = {
  pluginInfo,

  musicSearch: {
    async search(keyword, page = 1) {
      return {
        list: [demoSong],
        page,
        limit: 30,
        total: 1,
        has_more: false
      }
    }
  },

  songList: {
    async getHotLists(limit = 10) {
      return [
        {
          id: "playlist_1",
          name: "示例歌单",
          img: "https://example.com/playlist.jpg",
          desc: "这是一个示例歌单",
          play_count: "1万+"
        }
      ]
    },

    async getListDetail(id, page = 1, limit = 30) {
      return {
        id,
        source: "demo",
        page,
        limit,
        total: 1,
        info: {
          name: "示例歌单",
          img: "https://example.com/playlist.jpg",
          desc: "这是一个示例歌单",
          author: "QZ"
        },
        list: [demoSong]
      }
    }
  },

  album: {
    async getListDetail(id) {
      return {
        source: "demo",
        total: 1,
        info: {
          id,
          name: "示例专辑",
          artist: "示例歌手",
          img: "https://example.com/album.jpg",
          description: "示例专辑介绍"
        },
        list: [demoSong]
      }
    }
  },

  async getUrl(songId, quality) {
    return "https://example.com/audio.mp3"
  },

  async getLyric(songId) {
    return "[00:00.00] 示例歌词"
  },

  hotSearch: {
    async getList() {
      return ["示例歌曲", "示例歌手"]
    }
  }
}
```
