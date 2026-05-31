# Multi-stage build for Symmetra applications
FROM node:20-alpine AS base
WORKDIR /app

# Install pnpm
RUN npm install -g pnpm

# ====================================================================
# Builder stage
# ====================================================================
FROM base AS builder

ARG APP_PATH=examples/symmify-mongo-crud

# Copy root package files
COPY package.json pnpm-lock.yaml pnpm-workspace.yaml ./

# Copy all packages
COPY packages ./packages
COPY ${APP_PATH} ./app

# Install dependencies
RUN pnpm install --frozen-lockfile

# Build packages
RUN cd packages/symmify && pnpm run build
RUN cd packages/mongo && pnpm run build || true
RUN cd packages/kafka && pnpm run build || true
RUN cd packages/rabbitmq && pnpm run build || true

# Build the application
WORKDIR /app/app
RUN pnpm run build 2>/dev/null || true

# ====================================================================
# Runtime stage
# ====================================================================
FROM base AS runtime

ARG APP_PATH=examples/symmify-mongo-crud

# Copy package files
COPY package.json pnpm-lock.yaml pnpm-workspace.yaml ./

# Copy packages
COPY packages ./packages
COPY ${APP_PATH} ./app

# Install production dependencies only
RUN pnpm install --frozen-lockfile --prod

# Build packages
RUN cd packages/symmify && pnpm run build
RUN cd packages/mongo && pnpm run build || true
RUN cd packages/kafka && pnpm run build || true
RUN cd packages/rabbitmq && pnpm run build || true

WORKDIR /app/app

# Expose default port
EXPOSE 3000

# Default command
ENV COMMAND="npm run dev"
CMD sh -c "$COMMAND"
