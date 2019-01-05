---
title: 数据库是如何工作的？
---

- 数据在磁盘和内存上的存储格式是怎样的？
- 何时将数据从内存写入磁盘？
- 为什么每个表只能有一个主键？
- 事务回滚是如何进行的？
- 数据库索引的格式是什么样的？
- 数据库何时会进行全表扫描？全表扫描是如何工作的？
- PREPARED 语句是以什么格式保存的？

简言之，就是数据库系统是如何**工作的**？

为了理解数据库是如何工作的，我将在本系列文件中介绍如何使用 C 语言从头构建一个类 [sqlite](https://www.sqlite.org/arch.html) 数据库。

# 目录

{% for part in site.parts %}- [{{part.title}}]({{site.baseurl}}{{part.url}})
{% endfor %}

> "What I cannot create, I do not understand." -- [Richard Feynman](https://en.m.wikiquote.org/wiki/Richard_Feynman)

{% include image.html url="assets/images/arch2.gif" description="sqlite 架构 (https://www.sqlite.org/arch.html)" %}
