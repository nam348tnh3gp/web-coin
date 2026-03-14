const Joi = require('joi');

const registerSchema = Joi.object({
    password: Joi.string().min(6).required()
});

const loginSchema = Joi.object({
    displayAddress: Joi.string().pattern(/^W_[a-f0-9]+$/).required(),
    password: Joi.string().required()
});

// ĐÃ SỬA: Bỏ min() và max() để không kiểm tra timestamp
const transactionSchema = Joi.object({
    from: Joi.string().required(),
    to: Joi.string().required(),
    amount: Joi.number().integer().positive().required(),
    timestamp: Joi.number().integer().required(), // Chỉ cần là number, không check range
    signature: Joi.string().required()
});

const blockSchema = Joi.object({
    height: Joi.number().integer().required(),
    transactions: Joi.array().required(),
    previousHash: Joi.string().required(),
    timestamp: Joi.number().integer().required(),
    nonce: Joi.number().integer().required(),
    hash: Joi.string().required(),
    minerAddress: Joi.string().pattern(/^W_/).required()
});

function validate(schema) {
    return (req, res, next) => {
        const { error } = schema.validate(req.body);
        if (error) {
            return res.status(400).json({ error: error.details[0].message });
        }
        next();
    };
}

module.exports = { validate, registerSchema, loginSchema, transactionSchema, blockSchema };