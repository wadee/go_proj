# 验证码设计文档

---

程序主要功能

1. 题目产生程序
2. 文字变图片程序
3. 自动更新服务
4. HTTP服务

后期将主要对 题目产生规则进行升级 图片生成增加干扰



### 1. 题目生成

随机生成“三加上12是多少”这样的题目，并同时返回答案。

`captcha/topic.go`

1. Num2CN =》 将数字变成文字，例如 12 =》 十二，20 =》 二十，23 =》二十三。
2. RandTopic =》
	1. 首先生成运算符号 +/-。
	2. 如果是 + 则 左右数字从 0-30 中选择。
	3. 如果是 - 则 左侧数字为 10 - 50，右侧数字为 0 - 左侧数字中随机数。
3. 生成随机题目：
	1. 题目的模板为 [左侧数字][运算符][右侧数字][等于]
	2. 将左右两侧数字分别进行随机，获取其汉字或者阿拉伯数字。其中阿拉伯数字长度恒为一。
	3. 获取剩余长度，默认总长度为 7，汉字一位为一个长度，例如 二十三 长度为 3。
	4. TopicParse 中。如果当前剩余长度为 1 ，则结果直接为 [左侧数字] 加/减 [右侧数字]，例如二十三减二十一。
	5. 如果当前剩余长度为 2，则随机选择当前运算符的描述文字，例如 + 有加/加上，如果是加的话最后等于的描述只有 是，如果为 加上，则结果变成 二十三减去十一。
	6. 其他情况依次类推，原则就是保证长度恒为 7。
		
### 文字变图片程序

将文字从文字变成验证码图片。（后期可以加上例如扭曲变形、干扰线条以及干扰线条、图案）

`captcha/image.go`

1. 将文字按照长度切割（文字长度为1，数字十以内以及大于十也都为1）。
2. 将每个文字进行随机字体选择，并旋转 -30 ~ +30 度，同时随机颜色（从配置的颜色中选择）。

### 自动更新服务

初始化产生 100 张验证码，每十秒根据之前10s的访问量决定是否更新验证码库。

`process/process.go` `process/container.go`

1. 初始化，随机生成 100 张验证码，并生成图片，并把图片的名称和结果的对应的关系写入内存[有序列表]。
2. 更新时（100张），如果前十秒访问数量较小，则停止。否则首先新建 worker 完成验证码，完成后对 List 加锁禁止读取。然后将新的图片名称结果对应关系写入列表尾部，并从列表的头部删除相同数量数据。将读取列表的指针向前偏移新建的数量，同时解除锁定。
3. 读取名称为从头到尾循环读取，例如 10 张验证码，index 为 9 时，下一调用则为 0。

### HTTP服务

1. 从更新服务维护的列表中获得数据，去除图片名称和结果，将图片内容base64后和结果一起返回。