const std = @import("std");
const posix = std.posix;
const log = std.log;
const time = std.time;
const c = @cImport({
    @cInclude("linux/videodev2.h");
});

const Buffer = struct {
    start: []align(std.heap.page_size_min) u8,
    length: usize,
};

pub const Webcam = struct {
    fd: posix.fd_t = undefined,
    buffers: []Buffer = undefined,
    devname: []const u8,
    width: u32,
    height: u32,
    framerate: u32,
    alc: std.mem.Allocator = undefined,
    save_file: std.fs.File = undefined,

    const MIN_BUFFERS = 5;


    //ffplay /home/anthony/Desktop/videos/Camera1.yuv -pixel_format yuyv422 -video_size 640x480


    pub fn init(self: *Webcam, alc: std.mem.Allocator, save_file: std.fs.File) !void {
        self.alc = alc;
        self.save_file = save_file;
        //Open Device
        self.fd = try posix.open(self.devname, .{ .ACCMODE = .RDWR }, 0);

      
        //set format
        var fmt: c.struct_v4l2_format = undefined;
        @memset(@as([*]u8, @ptrCast(&fmt))[0..@sizeOf(c.struct_v4l2_format)], 0);
        fmt.type = c.V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = self.width;
        fmt.fmt.pix.height = self.height;
        fmt.fmt.pix.pixelformat = c.V4L2_PIX_FMT_YUV422P;
        //fmt.fmt.pix.field = c.V4L2_FIELD_ANY;
        try self.xioctl(c.VIDIOC_S_FMT, @intFromPtr(&fmt));
   
        //validate format
        @memset(@as([*]u8, @ptrCast(&fmt))[0..@sizeOf(c.struct_v4l2_format)], 0);
        fmt.type = c.V4L2_BUF_TYPE_VIDEO_CAPTURE;
        try self.xioctl(c.VIDIOC_G_FMT, @intFromPtr(&fmt));
        const p = fmt.fmt.pix.pixelformat;
        std.debug.print("Pixel Format: {c}{c}{c}{c}\n", .{ @as(u8, @truncate(p)), @as(u8, @truncate(p >> 8)), @as(u8, @truncate(p >> 16)), @as(u8, @truncate(p >> 24)) });

        @memset(@as([*]u8, @ptrCast(&fmt))[0..@sizeOf(c.struct_v4l2_format)], 0);
        fmt.type = c.V4L2_BUF_TYPE_VIDEO_CAPTURE;
        try self.xioctl(c.VIDIOC_G_FMT, @intFromPtr(&fmt));
        log.info("Camera format set to {d}x{d}\n", .{ fmt.fmt.pix.width, fmt.fmt.pix.height });

        //prepare buffers
        var req: c.struct_v4l2_requestbuffers = undefined;
        @memset(@as([*]u8, @ptrCast(&req))[0..@sizeOf(c.struct_v4l2_requestbuffers)], 0);
        req.count = MIN_BUFFERS;
        req.type = c.V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = c.V4L2_MEMORY_MMAP;
        try self.xioctl(c.VIDIOC_REQBUFS, @intFromPtr(&req));
        if (req.count < MIN_BUFFERS) {
            log.err("Insufficient buffer memory on camera\n", .{});
            unreachable;
        }
        self.buffers = try self.alc.alloc(Buffer, req.count);
        for (self.buffers, 0..) |_, i| {
            var buff: c.struct_v4l2_buffer = undefined;
            @memset(@as([*]u8, @ptrCast(&buff))[0..@sizeOf(c.struct_v4l2_buffer)], 0);
            buff.type = c.V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buff.memory = c.V4L2_MEMORY_MMAP;
            //buff.index = @as(c_uint, @truncate(i));
            buff.index = @intCast(i);
            try self.xioctl(c.VIDIOC_QUERYBUF, @intFromPtr(&buff));
            self.buffers[i].length = buff.length;
            self.buffers[i].start = try posix.mmap(null, buff.length, posix.PROT.READ | posix.PROT.WRITE, .{ .TYPE = .SHARED }, self.fd, buff.m.offset);
        }
        for (self.buffers, 0..) |_, i| {
            try self.enqueueBuffer(i);
        }
    }

    pub fn start(self: *Webcam) void {
        const t: c.enum_v4l2_buf_type = c.V4L2_BUF_TYPE_VIDEO_CAPTURE;
        self.xioctl(c.VIDIOC_STREAMON, @intFromPtr(&t)) catch unreachable;
    }

    pub fn stop(self: *Webcam) void {
        const t: c.enum_v4l2_buf_type = c.V4L2_BUF_TYPE_VIDEO_CAPTURE;
        self.xioctl(c.VIDIOC_STREAMOFF, @intFromPtr(&t)) catch unreachable;
    }
    pub fn deinit(self: *Webcam) void {
        for (self.buffers) |buf| {
            _ = std.os.linux.munmap(buf.start.ptr, buf.length);
        }
        _ = posix.close(self.fd);
        self.alc.free(self.buffers);
    }

    fn enqueueBuffer(self: *Webcam, index: usize) !void {
        var buf: c.struct_v4l2_buffer = undefined;
        @memset(@as([*]u8, @ptrCast(&buf))[0..@sizeOf(c.struct_v4l2_buffer)], 0);
        buf.type = c.V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = c.V4L2_MEMORY_MMAP;
        buf.index = @as(c_uint, @truncate(index));
        try self.xioctl(c.VIDIOC_QBUF, @intFromPtr(&buf));
    }

    fn xioctl(self: *Webcam, request: u32, arg: usize) !void {
        var rc: usize = undefined;
        while (true) {
            rc = std.os.linux.ioctl(self.fd, request, arg);
            switch (posix.errno(rc)) {
                .SUCCESS => return,
                .INTR => continue,
                else => |err| return posix.unexpectedErrno(err),
            }
        }
    }
    pub fn capture(self: *Webcam) !void {
        var buf: c.struct_v4l2_buffer = undefined;
        @memset(@as([*]u8, @ptrCast(&buf))[0..@sizeOf(c.struct_v4l2_buffer)], 0);
        buf.type = c.V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = c.V4L2_MEMORY_MMAP;

        try self.xioctl(c.VIDIOC_DQBUF, @intFromPtr(&buf));
        const b = self.buffers[buf.index];
        //std.debug.print("Bytes: {any}\n", .{buf.bytesused});
        
        try self.save_file.writeAll(b.start[0..buf.bytesused]);

        try self.enqueueBuffer(buf.index);
    }
};
