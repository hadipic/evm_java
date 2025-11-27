var CONFIG = {
    SYSTEM: {
        reset: "\x1b[0m",
        bold: "\x1b[1m",
        dim: "\x1b[2m",
        italic: "\x1b[3m",
        underscore: "\x1b[4m",
        reverse: "\x1b[7m",
        strikethrough: "\x1b[9m",
        backoneline: "\x1b[1A",
        cleanthisline: "\x1b[K"
    },
    FONT: {
        black: "\x1b[30m",
        red: "\x1b[31m",
        green: "\x1b[32m",
        yellow: "\x1b[33m",
        blue: "\x1b[34m",
        magenta: "\x1b[35m",
        cyan: "\x1b[36m",
        white: "\x1b[37m",
    },
    BACKGROUND: {
        black: "\x1b[40m",
        red: "\x1b[41m",
        green: "\x1b[42m",
        yellow: "\x1b[43m",
        blue: "\x1b[44m",
        magenta: "\x1b[45m",
        cyan: "\x1b[46m",
        white: "\x1b[47m"
    }
};

// Sequence of levels is important.
var LEVELS = ["debug", "info", "warn", "error", "disable"];

function Logger() {
    // Current command
    this.command = '';
    // Last line
    this.lastCommand = '';
    // set level from env
    var level = 'debug';

    this.isLevelValid = function(level) {
        return LEVELS.includes(level);
    }

    if (this.isLevelValid(level)) {
        this.level = level;
    }

    this.noColor = false;
    // this._getDate = () => (new Date()).toISOString();
    this._getDate = function() {
        return "";
    }

    this.setLevel = function(level) {
        if (this.isLevelValid(level)) {
            this.level = level;
        } else {
            throw "Level you are trying to set is invalid";
        }
    }

    this.setLevelNoColor = function() {
        this.noColor = true;
    }

    this.setLevelColor = function() {
        this.noColor = false;
    }

    this.isAllowedLevel = function(level) {
        return this.level ? LEVELS.indexOf(this.level) <= LEVELS.indexOf(level) : true
    }

    this.log = function() {
        var args = arguments;
        for (var idx = 0; idx < args.length; idx++) {
            var arg = args[idx];
            if (typeof arg === "string") {
                this.command += arg;
            } else {
                this.command += JSON.stringify(arg);
            }
            if (args.length > 1 && idx < args.length - 1) {
                this.command += " ";
            }
        }

        if (!this.noColor) {
            this.command += CONFIG.SYSTEM.reset;
        }
        console.log(this.command);
        // Save last command if we need to use for joint
        this.lastCommand = this.command;
        this.command = '';
        return this;
    }

    this.joint = function() {
        // Clear the last line
        console.log(CONFIG.SYSTEM.backoneline + CONFIG.SYSTEM.cleanthisline);

        // Reset the command to let it joint the next
        // And print from the position of last line
        this.command = '';

        // if joint more than twice, we should clean the previous
        // backline command, since we should only do it for the
        // current time.
        this.lastCommand = this.lastCommand.replace(CONFIG.SYSTEM.backoneline, "");

        // back to the last line
        this.command += CONFIG.SYSTEM.backoneline;

        this.command += this.lastCommand;
        return this;
    }

    this.setDate = function(callback) {
        this._getDate = callback;
    }

    this.getDate = function() {
        return this._getDate();
    }

    this.color = function(ticket) {
        if (ticket in CONFIG.FONT) {
            this.command += CONFIG.FONT[ticket];
        } else {
            this.warn("node-color-log: Font color not found! Use the default.")
        }
        return this;
    }

    this.bgColor = function(ticket) {
        if (ticket in CONFIG.BACKGROUND) {
            this.command += CONFIG.BACKGROUND[ticket];
        } else {
            this.warn("node-color-log: Background color not found! Use the default.")
        }
        return this;
    }

    this.bold = function() {
        this.command += CONFIG.SYSTEM.bold;
        return this;
    }

    this.dim = function() {
        this.command += CONFIG.SYSTEM.dim;
        return this;
    }

    this.underscore = function() {
        this.command += CONFIG.SYSTEM.underscore;
        return this;
    }

    this.strikethrough = function() {
        this.command += CONFIG.SYSTEM.strikethrough;
        return this;
    }

    this.reverse = function() {
        this.command += CONFIG.SYSTEM.reverse;
        return this;
    }

    this.italic = function() {
        this.command += CONFIG.SYSTEM.italic;
        return this;
    }

    this.fontColorLog = function(ticket, text, setting) {
        var command = '';
        if (setting) {
            command += this.checkSetting(setting);
        }
        if (ticket in CONFIG.FONT) {
            command += CONFIG.FONT[ticket];
        } else {
            this.warn("node-color-log: Font color not found! Use the default.")
        }
        command += text;
        command += CONFIG.SYSTEM.reset;
        console.log(command);
    }

    this.bgColorLog = function(ticket, text, setting) {
        var command = '';
        if (setting) {
            command += this.checkSetting(setting);
        }
        if (ticket in CONFIG.BACKGROUND) {
            command += CONFIG.BACKGROUND[ticket];
        } else {
            this.warn("node-color-log: Background color not found! Use the default.")
        }
        command += text;
        command += CONFIG.SYSTEM.reset;
        console.log(command);
    }

    this.colorLog = function(ticketObj, text, setting) {
        var command = '';
        if (setting) {
            command += this.checkSetting(setting);
        }
        if (ticketObj.font in CONFIG.FONT) {
            command += CONFIG.FONT[ticketObj.font];
        } else {
            this.warn("node-color-log: Font color not found! Use the default.")
        }
        if (ticketObj.bg in CONFIG.BACKGROUND) {
            command += CONFIG.BACKGROUND[ticketObj.bg]
        } else {
            this.warn("node-color-log: Background color not found! Use the default.")
        }

        command += text;
        command += CONFIG.SYSTEM.reset;
        console.log(command);
    }

    this.error = function() {
        if (!this.isAllowedLevel("error"))
            return this;

        var args = Array.prototype.slice.call(arguments);
        
        if (this.noColor) {
            var d = this.getDate();
            this.log.apply(this, [d, " [ERROR] "].concat(args));
        } else {
            var d = this.getDate();
            this.log(d + " ").joint()
                .bgColor('red').log('[ERROR]').joint()
                .log(" ").joint()
                .color('red').log.apply(this, args);
        }
        return this;
    }

    this.warn = function() {
        if (!this.isAllowedLevel("warn"))
            return this;

        var args = Array.prototype.slice.call(arguments);
        
        if (this.noColor) {
            var d = this.getDate();
            this.log.apply(this, [d, " [WARN] "].concat(args));
        } else {
            var d = this.getDate();
            this.log(d + " ").joint()
                .bgColor('yellow').log('[WARN]').joint()
                .log(" ").joint()
                .color('yellow').log.apply(this, args);
        }
        return this;
    }

    this.info = function() {
        if (!this.isAllowedLevel("info"))
            return this;

        var args = Array.prototype.slice.call(arguments);
        
        if (this.noColor) {
            var d = this.getDate();
            this.log.apply(this, [d, " [INFO] "].concat(args));
        } else {
            var d = this.getDate();
            this.log(d + " ").joint()
                .bgColor('green').log('[INFO]').joint()
                .log(" ").joint()
                .color('green').log.apply(this, args);
        }
        return this;
    }

    this.debug = function() {
        if (!this.isAllowedLevel("debug"))
            return this;

        var args = Array.prototype.slice.call(arguments);
        
        if (this.noColor) {
            var d = this.getDate();
            this.log.apply(this, [d, " [DEBUG] "].concat(args));
        } else {
            var d = this.getDate();
            this.log(d + " ").joint()
                .bgColor('cyan').log("[DEBUG]").joint()
                .log(' ').joint()
                .color('cyan')
                .log.apply(this, args);
        }
        return this;
    }

    this.checkSetting = function(setting) {
        var validSetting = ['bold', 'italic', 'dim', 'underscore', 'reverse', 'strikethrough'];
        var command = '';
        var keys = Object.keys(setting);
        for (var idx = 0; idx < keys.length; idx++) {
            var item = keys[idx]
            if (validSetting.indexOf(item) !== -1) {
                if (setting[item] === true) {
                    command += CONFIG.SYSTEM[item];
                } else if (setting[item] !== false) {
                    this.warn("node-color-log: The value " + item + " should be boolean.")
                }
            } else {
                this.warn("node-color-log: " + item + " is not valid in setting.")
            }
        }
        return command;
    }
}

// ایجاد instance و export
var logger = new Logger();

// سازگار با محیط EVM
if (typeof module !== 'undefined' && module.exports) {
    module.exports = logger;
} else {
    // برای محیط global
    globalThis.logger = logger;
}

// تست logger
logger.debug("Debug message");
logger.info("Info message"); 
logger.warn("Warning message");
logger.error("Error message");
