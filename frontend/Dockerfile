FROM node:20-alpine
WORKDIR /app
RUN echo '{"name":"rs-frontend","version":"0.1.0"}' > package.json
RUN echo '<html><body><h1>RS Pipeline Frontend - Coming Soon</h1></body></html>' > index.html
EXPOSE 3000
CMD ["npx", "serve", "-p", "3000", "."]