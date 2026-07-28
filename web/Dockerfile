# Build stage
FROM node:22-alpine AS builder

WORKDIR /app

COPY package.json package-lock.json* ./
RUN npm install

COPY . .
RUN npm run build

# Runtime stage
FROM node:22-alpine

WORKDIR /app

COPY --from=builder /app/package.json package-lock.json* ./
COPY --from=builder /app/.next ./.next
COPY --from=builder /app/node_modules ./node_modules
COPY --from=builder /app/public ./public
COPY --from=builder /app/server.mjs ./

# 3000: public. 3001: internal-only catalog API (design doc §8.1) — must
# never be published in docker-compose.yml's `ports:` mapping.
EXPOSE 3000 3001

CMD ["node", "server.mjs"]
